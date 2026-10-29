// TabPage — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3). Sidan är en
// Panel vars rubrik flikkontrollen ritar (tab_control.cpp); här bor bara
// rubrikens sättare och sidans egen — nästan tomma — ritning. Inga tal och
// inga färger i den här filen.

#include "glaze/widgets/tab_page.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("tabpage", TabPage)

// Rubriken ritas i FÖRÄLDERNS flikrad och bestämmer flikens bredd — så det
// är föräldern som ska ritas om, inte sidans egen yta.
void TabPage::setText(TrText t)
{
    if (text_ == t)
        return;
    text_ = std::move(t);
    if (parent())
        parent()->invalidate();
    else
        invalidate();
}

bool TabPage::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    return Panel::setProp(key, value);   // basen SIST — konventionen
}

// YTAN ÄR FLIKKONTROLLENS (headern): sidan ritar bara sin ram, när den är
// satt, och då exakt som en Panel. ALLA TRE grenarna gör samma sak, och det
// är ett beslut och inte en lucka — flikkontrollen har redan fyllt och ramat
// in visningsytan i familjens form, och Minimalistics panelfyllning hade
// bara lagt samma färg en gång till ovanpå.
void TabPage::draw(const Ctx& c)
{
    switch (c.ui) {
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default:
        if (isBorder())
            Panel::draw(c);
        break;
    }
}

} // namespace glaze
