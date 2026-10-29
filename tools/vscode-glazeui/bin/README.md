# bin/ — buntade binärer (committas INTE)

En underkatalog per VSIX-target; paketeringsscriptet buntar binärerna för
exakt EN target. Varje target har TVÅ binärer — glaze-render (rendering) och
GlazeBuilder (editorn):

```
bin/darwin-arm64/glaze-render     +  GlazeBuilder.app/   Mac Apple Silicon (verifierad väg)
bin/darwin-x64/glaze-render       +  GlazeBuilder.app/   Mac Intel
bin/win32-x64/glaze-render.exe    +  GlazeBuilder.exe    Windows
bin/linux-x64/glaze-render        +  GlazeBuilder        Linux
```

glaze-render är ett CLI (enkel binär, inget bundle ens på Mac); GlazeBuilder
är en GUI-app och blir en `.app`-bundle på Mac.

Binärerna byggs på **respektive plattform** ur repot (`cmake --build build`
→ `build/apps/{glaze-render,GlazeBuilder}/`) och läggs hit med
`node scripts/copy-bin.mjs <target>`. Saknas någon av dem vägrar
`npm run package:<target>` — den VSIX:en byggs när båda finns.

Binärerna är STATISKA. `copy-bin.mjs` läser `build/CMakeCache.txt` och vägrar
ett `build/` byggt med `GLAZE_SHARED=ON` — en exe vars libglaze.dll/.so/.dylib
aldrig följer med VSIX:en hade inte startat hos användaren.
