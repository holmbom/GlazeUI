// component_tree — se .h.

#include "component_tree.h"

#include <optional>

#include <glaze/glaze.h>

#include "apply_props.h"

namespace bld {

std::vector<PreviewComponent> buildComponents(const WindowModel& m,
                                              const char*        origin)
{
    std::vector<PreviewComponent> ut;
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const WidgetNode& n = m.nodes[i];
        // Trädets noder — window_tree.h respektive builderns pass 1.
        if (n.builtin || m.isAdopted(n.id))
            continue;

        const std::optional<glaze::PreviewForm> form =
            glaze::componentPreviewForm(n.type);
        if (!form) {
            gfx::log("%s: '%s' (%s) är varken adopterad eller en känd "
                     "komponent — utförs inte",
                     origin, n.id.c_str(), n.type.c_str());
            continue;
        }
        if (*form != glaze::PreviewForm::Inert) {
            gfx::log("%s: '%s' (%s) är en komponent utan inert previewform — "
                     "satserna bevaras och utförs inte",
                     origin, n.id.c_str(), n.type.c_str());
            continue;
        }
        PreviewComponent pc{i, glaze::makeComponent(n.type)};
        if (!pc.object) {
            gfx::log("%s: '%s' (%s) — komponentens fabrik svarade tomt, "
                     "utförs inte",
                     origin, n.id.c_str(), n.type.c_str());
            continue;
        }

        // NAMNET = MEDLEMSNAMNET, som för en widget: den kanoniska
        // `name`-raden sätter samma sträng igen bland satserna.
        pc.object->name = n.id;

        // SATSERNA i KÄLLORDNING — en sättare och ett anrop på samma
        // komponent kan bero av varandra (`setInterval` före `start`).
        for (const Stmt& s : m.stmts)
            if (s.target == n.id &&
                (s.kind == Stmt::Kind::Prop || s.kind == Stmt::Kind::Call))
                applyComponentStmt(*pc.object, s, origin);
        ut.push_back(std::move(pc));
    }
    return ut;
}

} // namespace bld
