# hlsdk-portable GameCube patches

`3rdparty/hlsdk-portable` (the game code: `dlls/` server, `cl_dll/` client) is
a separate checkout of FWGS hlsdk-portable and is not tracked by this repo.
These patches are its GameCube commits, exported so the work lives on master.

Base: FWGS `mobile_hacks` at `079f2387eb59e4a045647d9057240628130f0058`.

Apply to a fresh checkout:

```
git clone -b mobile_hacks https://github.com/FWGS/hlsdk-portable.git 3rdparty/hlsdk-portable
git -C 3rdparty/hlsdk-portable checkout 079f2387eb59e4a045647d9057240628130f0058
git -C 3rdparty/hlsdk-portable am ../../patches/hlsdk-portable/*.patch
```

Refresh after new commits in the checkout:

```
rm patches/hlsdk-portable/*.patch
git -C 3rdparty/hlsdk-portable format-patch origin/mobile_hacks..HEAD -o ../../patches/hlsdk-portable
```
