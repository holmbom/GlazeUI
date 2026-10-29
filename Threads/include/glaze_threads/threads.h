#pragma once

// glaze_threads — PARAPLYET. Allt kärnan har:
//
//   Result<T>   värde ELLER läsbart fel           glaze_threads/result.h
//   Context     var en fortsättning körs          glaze_threads/context.h
//   Pool        fast antal trådar, FIFO-kö        glaze_threads/pool.h
//   Worker      arbete → svar i kontexten         glaze_threads/worker.h
//
// UI-bryggan (`uiContext()`) står i glaze_threads/ui.h och kräver targetet
// `glaze_threads_ui` — det enda i paketet som länkar `glaze`.

#include "glaze_threads/context.h"
#include "glaze_threads/pool.h"
#include "glaze_threads/result.h"
#include "glaze_threads/worker.h"
