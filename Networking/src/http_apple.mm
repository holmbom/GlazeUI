// glaze_net — HTTP(S) på MACOS genom NSURLSession. Kontraktet står i
// http.h; valideringen och likriktningen i http.cpp.
//
// Identifierarna är ASCII med flit — ingen UTF-8-identifierare i en .mm.
//
// FORMEN: EN delad session för hela processen (Apple avråder från en
// session per förfrågan — anslutningar återanvänds inom sessionen), med EN
// delegat som slår upp förfrågans tillstånd på uppgiftens identifierare.
// Pooltråden skapar uppgiften, registrerar tillståndet FÖRE `resume` och
// väntar sedan med tak och stopp; delegaten (på sessionens seriella kö)
// fyller i svaret och väcker väntan när uppgiften är klar. Avbrytning och
// tak = `[task cancel]`, som är trådsäker — svaret väntas ändå in, så att
// delegaten aldrig skriver i ett tillstånd som ingen läser.
//
// LIKRIKTNINGEN mot WinHTTP och libcurl, gjord i sessionens konfiguration:
// ingen cache (URLCache = nil), inga automatiska kakor (HTTPCookieStorage =
// nil), högst 10 omdirigeringar, och https → http vägras i delegaten (det
// blir 3xx-svaret, som http.cpp gör till samma fel som de andra ger).
//
// KÄND SKILLNAD: App Transport Security. En app utan undantag i sin
// Info.plist får bara ladda https; http:// (och från macOS 14 en
// IP-adress) stoppas av OS:et med
// NSURLErrorAppTransportSecurityRequiresSecureConnection, som här blir
// `Code::Permission` med förklaringen i texten.

#import <Foundation/Foundation.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "http_impl.h"
#include "sys.h"

namespace glaze::net::detail {
namespace {

struct State {
    std::mutex                  m;
    std::condition_variable_any cv;
    bool                        done = false;
    HttpResponse                response;
    bool                        follow      = true;
    bool                        originalTls = false;
    int                         redirects   = 0;
    bool                        tooMany     = false;
    NSInteger                   errorCode   = 0;   // NSURLErrorDomain-kod
    bool                        failed      = false;
    std::string                 errorText;
};

} // namespace
} // namespace glaze::net::detail

using glaze::net::detail::State;

// Uppgiftens tillstånd i en ObjC-låda, så att det kan ligga i en NSDictionary.
@interface GlazeNetBox : NSObject {
@public
    std::shared_ptr<State> state;
}
@end

@implementation GlazeNetBox
@end

@interface GlazeNetDelegate : NSObject <NSURLSessionDataDelegate>
- (void)registerTask:(NSURLSessionTask*)task state:(std::shared_ptr<State>)state;
@end

@implementation GlazeNetDelegate {
    NSMutableDictionary<NSNumber*, GlazeNetBox*>* _boxes;
}

- (instancetype)init
{
    if ((self = [super init]))
        _boxes = [NSMutableDictionary dictionary];
    return self;
}

- (void)registerTask:(NSURLSessionTask*)task state:(std::shared_ptr<State>)state
{
    GlazeNetBox* box = [GlazeNetBox new];
    box->state       = std::move(state);
    @synchronized(self) {
        _boxes[@(task.taskIdentifier)] = box;
    }
}

- (std::shared_ptr<State>)stateFor:(NSURLSessionTask*)task remove:(BOOL)remove
{
    @synchronized(self) {
        NSNumber*    key = @(task.taskIdentifier);
        GlazeNetBox* box = _boxes[key];
        if (!box)
            return nullptr;
        std::shared_ptr<State> s = box->state;
        if (remove)
            [_boxes removeObjectForKey:key];
        return s;
    }
}

- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler
{
    std::shared_ptr<State> s = [self stateFor:task remove:NO];
    if (!s) {
        completionHandler(request);
        return;
    }
    bool refuse = false;
    {
        std::lock_guard<std::mutex> lock(s->m);
        if (!s->follow) {
            refuse = true;   // 3xx-svaret är svaret
        } else if (s->originalTls &&
                   [request.URL.scheme caseInsensitiveCompare:@"http"] == NSOrderedSame) {
            refuse = true;   // nedgradering — http.cpp gör 3xx-svaret till felet
        } else if (++s->redirects > 10) {
            s->tooMany = true;
            refuse     = true;
        }
    }
    completionHandler(refuse ? nil : request);
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
    std::shared_ptr<State> s = [self stateFor:dataTask remove:NO];
    if (s && [response isKindOfClass:[NSHTTPURLResponse class]]) {
        NSHTTPURLResponse*          http = (NSHTTPURLResponse*)response;
        std::lock_guard<std::mutex> lock(s->m);
        s->response.status = static_cast<int>(http.statusCode);
        s->response.url    = http.URL.absoluteString ? http.URL.absoluteString.UTF8String : "";
        s->response.headers.clear();
        s->response.body.clear();   // multipart/x-mixed-replace: varje del börjar om
        [http.allHeaderFields enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL*) {
          if ([key isKindOfClass:[NSString class]] && [value isKindOfClass:[NSString class]])
              s->response.headers.push_back({std::string([(NSString*)key UTF8String]),
                                             std::string([(NSString*)value UTF8String])});
        }];
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveData:(NSData*)data
{
    std::shared_ptr<State> s = [self stateFor:dataTask remove:NO];
    if (!s)
        return;
    std::lock_guard<std::mutex> lock(s->m);
    [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL*) {
      s->response.body.append(static_cast<const char*>(bytes), range.length);
    }];
}

- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error
{
    std::shared_ptr<State> s = [self stateFor:task remove:YES];
    if (!s)
        return;
    std::lock_guard<std::mutex> lock(s->m);
    if (error) {
        s->failed    = true;
        s->errorCode = [error.domain isEqualToString:NSURLErrorDomain] ? error.code : 0;
        s->errorText = error.localizedDescription ? error.localizedDescription.UTF8String
                                                  : "okänt fel";
        s->errorText += " (" + std::string(error.domain.UTF8String) + " " +
                        std::to_string(static_cast<long>(error.code)) + ")";
    }
    s->done = true;
    s->cv.notify_all();
}

@end

namespace glaze::net::detail {
namespace {

struct Session {
    NSURLSession*     session  = nil;
    GlazeNetDelegate* delegate = nil;
};

// Processens enda session. Den ogiltigförklaras aldrig och lever processen
// ut — en ogiltigförklarad session går inte att återanvända.
const Session& session()
{
    static const Session s = [] {
        Session r;
        NSURLSessionConfiguration* cfg =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.URLCache                   = nil;
        cfg.requestCachePolicy         = NSURLRequestReloadIgnoringLocalCacheData;
        cfg.HTTPCookieStorage          = nil;
        cfg.HTTPShouldSetCookies       = NO;
        cfg.timeoutIntervalForResource = 7 * 24 * 3600;   // taket sätts per förfrågan
        r.delegate                     = [GlazeNetDelegate new];
        r.session                      = [NSURLSession sessionWithConfiguration:cfg
                                                  delegate:r.delegate
                                             delegateQueue:nil];
        return r;
    }();
    return s;
}

Code koda(NSInteger code)
{
    switch (code) {
    case NSURLErrorTimedOut:
        return Code::Timeout;
    case NSURLErrorCannotFindHost:
    case NSURLErrorDNSLookupFailed:
        return Code::HostNotFound;
    case NSURLErrorCannotConnectToHost:
        return Code::Refused;
    case NSURLErrorNetworkConnectionLost:
        return Code::Reset;
    case NSURLErrorNotConnectedToInternet:
    case NSURLErrorInternationalRoamingOff:
    case NSURLErrorDataNotAllowed:
        return Code::Unreachable;
    case NSURLErrorSecureConnectionFailed:
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateNotYetValid:
    case NSURLErrorClientCertificateRejected:
    case NSURLErrorClientCertificateRequired:
        return Code::Tls;
    case NSURLErrorBadURL:
    case NSURLErrorUnsupportedURL:
        return Code::InvalidArgument;
    case NSURLErrorHTTPTooManyRedirects:
    case NSURLErrorBadServerResponse:
    case NSURLErrorCannotParseResponse:
    case NSURLErrorCannotDecodeContentData:
    case NSURLErrorCannotDecodeRawData:
    case NSURLErrorZeroByteResource:
        return Code::Protocol;
    case NSURLErrorAppTransportSecurityRequiresSecureConnection:
        return Code::Permission;
    default:
        return Code::Failed;
    }
}

} // namespace

Result<HttpResponse> perform(const HttpRequest& req, const Url& url, const std::stop_token& stop)
{
    @autoreleasepool {
        const Session& s = session();
        NSURL* nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url.full.c_str()]];
        if (!nsurl)
            return Error{Code::InvalidArgument, "NSURL vägrade url:en"};

        NSMutableURLRequest* r = [NSMutableURLRequest requestWithURL:nsurl];
        r.HTTPMethod           = [NSString stringWithUTF8String:req.method.c_str()];
        r.cachePolicy          = NSURLRequestReloadIgnoringLocalCacheData;
        if (req.timeout != kForever)
            r.timeoutInterval = std::max(0.001, static_cast<double>(req.timeout.count()) / 1000.0);
        if (!req.body.empty())
            r.HTTPBody = [NSData dataWithBytes:req.body.data() length:req.body.size()];
        [r setValue:@"glaze-net/1" forHTTPHeaderField:@"User-Agent"];
        for (const Header& h : req.headers)
            [r addValue:[NSString stringWithUTF8String:h.value.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:h.name.c_str()]];

        auto st         = std::make_shared<State>();
        st->follow      = req.followRedirects;
        st->originalTls = url.https;

        NSURLSessionDataTask* task = [s.session dataTaskWithRequest:r];
        [s.delegate registerTask:task state:st];
        [task resume];

        const sys::Deadline deadline  = sys::deadlineFrom(req.timeout);
        bool                cancelled = false;
        bool                timedOut  = false;
        {
            std::unique_lock<std::mutex> lock(st->m);
            const auto isDone = [&] { return st->done; };
            const bool got    = deadline == sys::Deadline::max()
                                    ? st->cv.wait(lock, stop, isDone)
                                    : st->cv.wait_until(lock, stop, deadline, isDone);
            if (!got) {
                cancelled = stop.stop_requested();
                timedOut  = !cancelled;
                lock.unlock();
                [task cancel];   // trådsäker; svaret (NSURLErrorCancelled) väntas in nedan
                lock.lock();
                st->cv.wait(lock, isDone);
            }
        }

        std::lock_guard<std::mutex> lock(st->m);
        if (cancelled)
            return Error{threads::Code::Cancelled, "avbrutet"};
        if (timedOut)
            return Error{Code::Timeout, "inget svar inom tidsgränsen"};
        if (st->tooMany)
            return Error{Code::Protocol, "för många omdirigeringar (fler än 10)"};
        if (st->failed) {
            if (st->errorCode == NSURLErrorCancelled)
                return Error{threads::Code::Cancelled, "avbrutet"};
            std::string text = st->errorText;
            if (st->errorCode == NSURLErrorAppTransportSecurityRequiresSecureConnection)
                text += " — App Transport Security: http:// kräver ett undantag i appens "
                        "Info.plist (NSAppTransportSecurity)";
            return Error{koda(st->errorCode), text};
        }
        if (st->response.url.empty())
            st->response.url = url.full;
        return std::move(st->response);
    }
}

} // namespace glaze::net::detail
