#pragma once

// GlazeUI — vårt eget widget-bibliotek. Apparna pratar bara med det här
// API:t; backend-maskineriet (fönster, fonter, event-routing och chrome)
// är gömt bakom glaze::AppWindow och gfx-fasaden. Samma utseende på Windows,
// macOS och Linux, eftersom Glaze ritar varenda pixel självt.
//
// Det här är PARAPLY-headern: apparna inkluderar <glaze/glaze.h> och får
// hela API:t. Varje widget bor i en egen header — öppna label.h/button.h/
// ... för en kopierbar mall till egna komponenter.
//
// Användning (normalvägen är GlazeBuilders tre-fils-modell — det här är
// den handskrivna miniformen):
//     class StartWindow : public glaze::AppWindow {
//     public:
//         StartWindow() : glaze::AppWindow("Titel", 800, 500) {
//             auto* btn = add<glaze::Button>("Test");
//             btn->width = 120; btn->height = 30;
//             btn->x = 10; btn->y = 10;
//             btn->onClick = [](glaze::Widget* sender, glaze::ClickArgs& e) { ... };
//         }
//     };
//
//     int main() {
//         StartWindow w;
//         return w.run();   // visar + pumpar tills w stängts (App skapas lat)
//     }

#include "glaze/assets.h"
#include "glaze/canvas/bitmap.h"
#include "glaze/canvas/camera.h"
#include "glaze/canvas/geometry.h"
#include "glaze/canvas/mesh.h"
#include "glaze/canvas/painter.h"
#include "glaze/canvas/path.h"
#include "glaze/canvas/render_texture.h"
#include "glaze/canvas/shape.h"
#include "glaze/canvas/style3d.h"
#include "glaze/color.h"
#include "glaze/color_dialog.h"
#include "glaze/component.h"
#include "glaze/draw.h"
#include "glaze/file_dialog.h"
#include "glaze/input.h"
#include "glaze/input_box.h"
#include "glaze/language.h"
#include "glaze/menu.h"
#include "glaze/message_box.h"
#include "glaze/print.h"
#include "glaze/profiler.h"
#include "glaze/stock_icon.h"
#include "glaze/strings.h"
#include "glaze/style.h"
#include "glaze/theme.h"
#include "glaze/timer.h"
#include "glaze/tray_icon.h"
#include "glaze/version.h"
#include "glaze/widget.h"

#include "glaze/widgets/app_icon.h"
#include "glaze/widgets/bar.h"
#include "glaze/widgets/button.h"
#include "glaze/widgets/canvas.h"
#include "glaze/widgets/caption_button.h"
#include "glaze/widgets/checkbox.h"
#include "glaze/widgets/color_picker.h"
#include "glaze/widgets/column_panel.h"
#include "glaze/widgets/data_grid.h"
#include "glaze/widgets/date_picker.h"
#include "glaze/widgets/dock_host.h"
#include "glaze/widgets/dock_panel.h"
#include "glaze/widgets/dropdown.h"
#include "glaze/widgets/file_picker.h"
#include "glaze/widgets/floating_panel.h"
#include "glaze/widgets/group_box.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/link.h"
#include "glaze/widgets/list_view.h"
#include "glaze/widgets/markdown_view.h"
#include "glaze/widgets/menu_bar.h"
#include "glaze/widgets/listbox.h"
#include "glaze/widgets/panel.h"
#include "glaze/widgets/picture.h"
#include "glaze/widgets/progress.h"
#include "glaze/widgets/property_grid.h"
#include "glaze/widgets/radio.h"
#include "glaze/widgets/scrollbar.h"
#include "glaze/widgets/separator.h"
#include "glaze/widgets/slider.h"
#include "glaze/widgets/spacer.h"
#include "glaze/widgets/spin_box.h"
#include "glaze/widgets/splitter.h"
#include "glaze/widgets/status_bar.h"
#include "glaze/widgets/tab_control.h"
#include "glaze/widgets/tab_page.h"
#include "glaze/widgets/table.h"
#include "glaze/widgets/text_area.h"
#include "glaze/widgets/textinput.h"
#include "glaze/widgets/tool_bar.h"
#include "glaze/widgets/tool_button.h"
#include "glaze/widgets/top_bar.h"
#include "glaze/widgets/tree_view.h"

#include "glaze/app.h"
#include "glaze/app_window.h"
