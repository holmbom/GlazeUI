#pragma once
#include <glaze/glaze.h>

// LoginWindow — `demo --login`, HANDKODAT: en inloggningsruta som visar
// fönstrets gradient, ljust blått i övre vänstra hörnet och vitt i de andra, och
// i mörkt läge ett dämpat blått mot bakgrunden. En glyfknapp i namnlisten växlar
// läge. Texterna är literaler, som i arbetsbänken för Slate: fönstret ingår inte i
// demons översatta yta.
class LoginWindow : public glaze::AppWindow {
public:
    LoginWindow();
};
