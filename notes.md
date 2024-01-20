# local Macos entitlement stuff


## debugging entitlement
Run:

```
$ codesign -s - -v -f --entitlements debug.plist $YOUR_BINARY
```

This gives the debugging entitlement so instruments can see memory allocations, etc.


## profiling

Run:

```
$ xctrace record --template "Time Profile"  --launch -- $YOUR_BINARY $ARG1 $ETC
```

You can then open the associated trace and see what is slow etc.

Instruments is pretty nice.

