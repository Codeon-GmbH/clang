# How to build mulle-clang

## What you get

* `mulle-clang` the mulle-clang the binary in `/usr/local/bin`.


### Prerequisites

You need a fairly current Unix, like Linux, OS X or FreeBSD. Or you can use
[MINGW](http://mingw.org/) on Windows.

Locate a place on your filesystem, where you have at least 20GB space free. You
probably need not bother to try, if you have less than 6GB of RAM. Or you risk
seeing `virtual memory exhausted: Cannot allocate memory`.

A docker container usually only has 10 GB of space. That is not enough to build
the compiler!

If you are configuring a virtual machine, give it some more cores!


### Windows: Installing further prerequisites

Check the
[mulle-build README.md](//www.mulle-kybernetik.com/software/git/mulle-build/README.md)
for instructions how to get the "Git for Windows" bash going.


### Installing on Linux or FreeBSD

You need the **bash** shell and you may want to get **sudo** happening to
install packages (or run the script as **root**). bash is already present.

Continue with [Common generic instructions](#common-generic).


### Installing on Windows 10

Firstly ensure, that your username does not contain anything else than
characters and underscores.

* mulle_nat : good
* nat : good
* Nat! : bad
* i am nat : very bad

You need to install some prerequisites first.

* Install [Visual Studio 2015 Community Edition](//beta.visualstudio.com/downloads/) or better (free). Make sure that you install Windows C++ support. Also add git support.
* [Git for Windows](//git-scm.com/download/win) is included in VS 2015, make sure it's there
* [Python 2 for Windows](//www.python.org/downloads/windows/). **Make sure that python is installed in **PATH**, which is not the default**
* [CMake for Windows](//cmake.org/download/). CMake should also add itself to **PATH**.

Reboot, so that Windows picks up the **PATH** changes (Voodoo).

Now the tricky part is to get the "Git bash" shell running with the proper VS
environment.  Assuming you kept default settings the "Git bash" is
`C:\Program Files\Git\git-bash.exe`. Open the "Developer Command Prompt for VS 2015"
from the start menu and execute the git-bash from there. A second window with
the bash should open.

Continue with [Common generic instructions](#common-generic).



<a name="common-generic"></a>
### Common generic instructions

Build and install using the `install-mulle-clang.sh` script.

The script downloads and builds the compiler and the dependencies. If your
machine is puny and weak, try to get pre-built binary packages from somewhere
as this project is huge.


1. Download the script, don't clone the whole repository yet

   ```
   mkdir mulle-clang
   cd mulle-clang
   curl -L -O "https://raw.githubusercontent.com/Codeon-GmbH/mulle-clang/mulle_objclang_39/install-mulle-clang.sh"
   ```
2. Build the compiler. Here is an example, where the compiler is installed into
   `opt` and the symlink to the compiler is installed in `/usr/local/bin`:

   ```
   chmod 755 install-mulle-clang.sh
   ./install-mulle-clang.sh --clang-prefix /opt
   ./install-mulle-clang.sh install --symlink-prefix /usr/local
   ```

   Now you will have to wait for a long, long time.
