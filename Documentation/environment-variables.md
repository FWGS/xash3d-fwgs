## Environment variables

#### Xash3D FWGS

The engine respects these environment variables:

| Variable              | Type       | Description |
| --------------------- | ---------- | ----------- |
| `XASH3D_GAME`         | _string_   | Overrides default game directory. Ignored if `-game` command line argument is set |
| `XASH3D_BASEDIR`      | _string_   | Sets path to base (root) directory, instead of current working directory |
| `XASH3D_RODIR`        | _string_   | Sets path to read-only base (root) directory. Ignored if `-rodir` command line argument is set |
| `XASH3D_EXTRAS_PAK1`  | _string_   | Archive file from specified path will be added to virtual filesystem search path in the lowest possible priority |
| `XASH3D_EXTRAS_PAK2`  | _string_   | Similar to `XASH3D_EXTRAS_PAK1` but next to it in priority list |

Environment variables NOT listed in the table above are used internally, and aren't considered as stable interface.

#### mdldec

| Variable              | Type       | Description |
| --------------------- | ---------- | ----------- |
| `MDLDEC_ACT_PATH`     | _string_   | If set, will read activities list from this path |
