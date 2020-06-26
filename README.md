[![Codeon Gmbh](CodeonLogo.png)](//www.codeon.de)

# mulle-clang

This is an Objective-C compiler based on clang 10.0.0, written for the
[mulle-objc](//www.mulle-kybernetik.com/weblog/2015/mulle_objc_a_new_objective_c_.html)
runtime. It corresponds to mulle-objc-runtime v0.17 or better.

> See [README.txt](README.txt) for more information about clang

The compiler can be used to:

1. compile Objective-C code for **mulle-objc**
2. compile C code

It is not recommended to use it for C++, since that is not tested.
It is not recommended to use it for other Objective-C runtimes, since there
have been some changes, that affect other runtimes.


## Operation

The compiler compiles Objective-C source for the **mulle-objc** runtime by
default. When compiling for **mulle-objc** the compiler will use the
[meta-ABI](//www.mulle-kybernetik.com/weblog/2015/mulle_objc_meta_call_convention.html)
for all method calls. The resultant `.o` files are linkable like any
other compiled C code.


### AAM - Always Autorelease Mode

The compiler has a special mode called AAM. This changes the Objective-C
language in the following ways:

1. There is a tranformation done on selector names

    Name           | Transformed Name
    ---------------|---------------------
    alloc          | instantiate
    new            | instantiatedObject
    copy           | immutableInstance
    mutableCopy    | mutableInstance
2. You can not access instance variables directly, but must use properties (or methods)
3. You can not do explicit memory management (like `-dealloc`, `-autorelease`,
`-release`, `-retain`, `-retainCount` etc.)

The transformed methods will return objects that are autoreleased. Hence the
name of the mode. The net effect is, that you have a mode that is ARC-like, yet
understandable and much simpler.

Your ARC code may not run in AAM, but AAM code should run in ARC with no
problems. If you can't do something in AAM, put it in a category in regular
Objective-C style.

The compiler handles `.aam` files, which enables AAM ("Always Autoreleased
Mode").

## Additional Compiler options and defined macros

Name                    | Compiler        | Default | Description
------------------------|-----------------|-------|--------------------
`__MULLE_OBJC__`        |  -              | -     | Compiling for mulle-objc
`__MULLE_OBJC_UNIVERSEID__`      | -fobjc-universename=name | -    | id of the universe, or 0 for default universe
`__MULLE_OBJC_UNIVERSENAME__`    | -fobjc-universename=name | -    | name of the universe, or NULL for default universe


The following table represents option pairs, that logically exclude each other.
Either one is always defined.

Name                    | Compiler        | Default | Description
------------------------|-----------------|-------|--------------------
`__MULLE_OBJC_AAM__`    | .aam file       | -     | AAM is enabled
`__MULLE_OBJC_NO_AAM__` | .m file         | -     | AAM is not enabled
 &nbsp;                 | &nbsp;          | &nbsp;|
`__MULLE_OBJC_TPS__`    | -fobjc-tps      | YES   | TPS (tagged pointer support) is enabled
`__MULLE_OBJC_NO_TPS__` | -fno-objc-tps   | NO    | TPS is not enabled
&nbsp;                  | &nbsp;          | &nbsp;|
`__MULLE_OBJC_FCS__`    | -fobjc-fcs      | YES   | FCS fast method/class support is enabled
`__MULLE_OBJC_NO_FCS__` | -fno-objc-fcs   | NO    | FCS is not enabled


## Macros used in Code Generation


The compiler output can be tweaked with the following preprocessor macros.
All macros must be defined with a simple integer, no expressions. All of them
are optional, unless indicated otherwise. The runtime, the Objective-C
Foundation on top of the runtime and the user application, will define them.


Name                                  | Description
--------------------------------------|--------------------------------------
`MULLE_OBJC_RUNTIME_VERSION_MAJOR`    | Major version of the runtime
`MULLE_OBJC_RUNTIME_VERSION_MINOR`    | Minor version of the runtime
`MULLE_OBJC_RUNTIME_VERSION_PATCH`    | Patch version of the runtime
`MULLE_OBJC_FOUNDATION_VERSION_MAJOR` | Major version of the Foundation
`MULLE_OBJC_FOUNDATION_VERSION_MINOR` | Minor version of the Foundation
`MULLE_OBJC_FOUNDATION_VERSION_PATCH` | Patch version of the Foundation
`MULLE_OBJC_USER_VERSION_MAJOR`       | User supplied version
`MULLE_OBJC_USER_VERSION_MINOR`       | User supplied of the Foundation
`MULLE_OBJC_USER_VERSION_PATCH`       | User supplied of the Foundation, all these version information values will be stored in the emitted object file.
`MULLE_OBJC_FASTCLASSHASH_0`          | First unique ID of a fast class
... | ...
`MULLE_OBJC_FASTCLASSHASH_63`         | Last unique ID of a fast class


## Functions used in Code Generation

These are the runtime functions used for method calling, retain/release
management, class lookup and exception handling. They are defined in the
runtime.

### All

Function                               | Memo
---------------------------------------|---------
`mulle_objc_exception_tryenter`        | `@throw`
`mulle_objc_exception_tryexit`         | `@finally`
`mulle_objc_exception_extract`         | `@catch`
`mulle_objc_exception_match`           | `@catch`


### -O0, -Os

Function                                            | Memo
----------------------------------------------------|-------------
`mulle_objc_object_call`                            | `[self foo:bar]`
`_mulle_objc_object_supercall`                      | `[super foo:bar]`
`mulle_objc_object_lookup_infraclass_nofail`        | `[Foo ...` for methods
`mulle_objc_object_lookup_infraclass_nofast_nofail` | `__MULLE_OBJC_NO_FCS__`
`mulle_objc_global_lookup_infraclass_nofail`        | `[Foo ...` for functions
`mulle_objc_global_lookup_infraclass_nofast_nofail` | `__MULLE_OBJC_NO_FCS__`


### -O1

Like -O0, but two functions are replaced:

Function                                            | Memo
----------------------------------------------------|-------------
`mulle_objc_object_partialinlinecall`               | `[self foo:bar]`
`_mulle_objc_object_partialinlinesupercall`         | `[super foo:bar]`


### -O2

Like -O1, but four functions are replaced with two, and two new functions are used:

Function                                            | Memo
----------------------------------------------------|-------------
`mulle_objc_object_inlinelookup_infraclass_nofail`  | `[Foo ...` for both FCS modes
`mulle_objc_global_inlinelookup_infraclass_nofail`  | `[Foo ...` for both FCS modes
`mulle_objc_object_inlineretain`                    | `[foo retain]`
`mulle_objc_object_inlinerelease`                   | `[foo release]`


### -O3

Like -O3, but two functions are replaced:

Function                                            | Memo
----------------------------------------------------|-------------
`mulle_objc_object_inlinecall`                      | `[self foo:bar]`
`_mulle_objc_object_inlinesupercall`                | `[super foo:bar]`



## Install

### OS X

You can use [homebrew](//brew.sh) to install the compiler (only):

```
brew install codeon-gmbh/software/mulle-clang
```

> If for some reason homebrew can not use the bottle, the compiler must be
> built from source. This takes a long time! On my Macbook Air the build
> can take an hour or more.


### Ubuntu

You can install mulle-clang with mulle-lldb via **apt-get** on Ubuntu:

```
sudo apt-get update &&
sudo apt-get install curl

curl -sS https://www.codeon.de/dists/codeon-pub.asc | sudo apt-key add -
echo "deb [arch=all] http://www.mulle-kybernetik.com `lsb_release -c -s` main" | sudo tee "/etc/apt/sources.list.d/mulle-kybernetik.com-main.list" > /dev/null

sudo apt-get update &&
sudo apt-get install mulle-clang
```

#### Debian

You can install on Debian (and Ubuntu) with `dpkg` after downloading the appropriate package. Notice that the
packages are still named with Ubuntu codes.

```
sudo dpkg --install mulle-clang-10.0.0.2-${Ubuntu}-amd64.deb
```

Debian  | Ubuntu  | `shasum -b -a 256`
--------|---------|-----------------------------------------------------------------
[bullseye](http://download.codeon.de/dists/focal/main/binary-amd64/mulle-clang-10.0.0.2-focal-amd64.deb)  | focal | `7305622a81ae3d2579f32622ef40e6fdd38d068fa717e4adf60b5ea9b98e6559`
[buster](http://download.codeon.de/dists/bionic/main/binary-amd64/mulle-clang-10.0.0.2-bionic-amd64.deb)    | bionic | `3d54a5398d4f3884372094199fea7f4e99c5aea9c0bd066009d855685623b97e`
[stretch](http://download.codeon.de/dists/xenial/main/binary-amd64/mulle-clang-10.0.0.2-xenial-amd64.deb) | xenial | `e0cfb43302cf9073786590f663a38e304b0350fe11e9f07b1cfd04bab23a56e9`
[stretch i386](http://download.codeon.de/dists/xenial/main/binary-i386/mulle-clang-10.0.0.2-xenial-i386.deb)  | xenial i386 | `86df0d2fbe578514b7f42344c24a7884d5c667b5a7731fb8a15bf4dbd5e8b761`


## Build
* [How to Build](BUILD_MULLE_CLANG.md)

Afterwards head on over to [mulle-objc](//github.com/mulle-objc) to get the
runtime libraries.


## Author

[Nat!](//www.mulle-kybernetik.com/weblog) for
[Codeon GmbH](//www.codeon.de)
