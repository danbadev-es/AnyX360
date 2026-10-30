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

## title_keys.txt

Dance Central 3 (and Dance Central 2) derive each song's key through the console's security engine:
`XeKeysSetKey` with a key from a table in the game's executable, then `XeKeysAesCbc`. The table holds
the keys in the form the console obscures them, and the console's hypervisor turns them back; AnyX360
can't, so a game that opts in (`GameDescriptor::title_keys`) looks the real values up in
`keys/title_keys.txt`. Without the file such a game doesn't start; a key the file lacks ends it with a
message. The release copies the file next to the retail key when it is here.

The file is made from a copy of the game whose executable already holds the real keys: the re-packed
Dance Central 3 disc dump the port was first made from had its key path patched to software AES with
them (it survives in the user's `.anyx` backup of that copy). Extract both executables with
`x360tool xex-extract`, then:

```
python -I tools\scripts\title_keys.py <retail image.bin> <keyed image.bin> 82D13000 4 keys\title_keys.txt --label "Dance Central 2 and 3 songs"
```

Each line is `<obscured> <real>` (32 hex digits each) and an optional `# comment`. Never commit it:
like the retail key, it isn't in the repository.
