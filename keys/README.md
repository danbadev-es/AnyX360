# keys/

This folder is git-ignored (except this file). AnyX360 does not ship any console keys.

`x360tool xex-extract` needs the 16-byte XEX2 retail key to decrypt retail executables. Put it in
`keys/xex2_retail.key` as either:

- a 16-byte binary file, or
- a text file containing 32 hex characters.

The key is public and widely known (it is in Xenia's source and in the XenonRecomp submodule,
`thirdparty/XenonRecomp/XenonUtils/xex.h`). AnyX360 keeps its own tools free of it so the repository
does not distribute it.

Development-kit executables use an all-zero key and need no file. The tool tries the retail key first and falls
back to the devkit key, then checks that the result begins with a valid `MZ`/PE header.
