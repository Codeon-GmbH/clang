#! /bin/sh
#
# mulle-clang installer
# (c) 2016 Codeon GmbH, coded by Nat!
# BSD-3 License

# various versions
MULLE_OBJC_VERSION_BRANCH=39
MULLE_OBJC_VERSION="3.9.0"  # for opt

CMAKE_VERSION="3.5"
CMAKE_VERSION_MAJOR=3
CMAKE_VERSION_MINOR=5
CMAKE_VERSION_PATCH=2
CMAKE_PATCH_VERSION="${CMAKE_VERSION}.2"

CLANG_ARCHIVE="https://github.com/Codeon-GmbH/mulle-clang/archive/3.9.0.tar.gz"
LLVM_ARCHIVE="http://www.llvm.org/releases/3.9.0/llvm-3.9.0.src.tar.xz"
LIBCXX_ARCHIVE="http://llvm.org/releases/3.9.0/libcxx-3.9.0.src.tar.xz"
LIBCXXABI_ARCHIVE="http://llvm.org/releases/3.9.0/libcxxabi-3.9.0.src.tar.xz"


environment_initialize()
{
   UNAME="`uname`"
   case "${UNAME}" in
      MINGW*)
         CLANG_SUFFIX="-cl"
         EXE_EXTENSION=".exe"
         SYMLINK_PREFIX="~"
         SUDO=
      ;;

      *)
         SYMLINK_PREFIX="/usr/local"
         SUDO="sudo"
      ;;
   esac
}


log_initialize()
{
   if [ -z "${NO_COLOR}" ]
   then
      case "${UNAME}" in
         Darwin|Linux|FreeBSD|MINGW*)
            C_RESET="\033[0m"

            # Useable Foreground colours, for black/white white/black
            C_RED="\033[0;31m"     C_GREEN="\033[0;32m"
            C_BLUE="\033[0;34m"    C_MAGENTA="\033[0;35m"
            C_CYAN="\033[0;36m"

            C_BR_RED="\033[0;91m"
            C_BOLD="\033[1m"
            C_FAINT="\033[2m"

            C_RESET_BOLD="${C_RESET}${C_BOLD}"
            trap 'printf "${C_RESET}"' TERM EXIT
         ;;
      esac
   fi
   C_ERROR="${C_RED}${C_BOLD}"
   C_INFO="${C_MAGENTA}${C_BOLD}"
   C_FLUFF="${C_GREEN}${C_BOLD}"
   C_VERBOSE="${C_CYAN}${C_BOLD}"
}


concat()
{
   local i
   local s

   for i in "$@"
   do
      if [ -z "${i}" ]
      then
         continue
      fi

      if [ -z "${s}" ]
      then
         s="${i}"
      else
         s="${s} ${i}"
      fi
   done

   echo "${s}"
}


log_error()
{
   printf "${C_ERROR}%b${C_RESET}\n" "$*" >&2
}


log_info()
{
   printf "${C_INFO}%b${C_RESET}\n" "$*" >&2
}


log_fluff()
{
   if [ ! -z "${FLUFF}" ]
   then
      printf "${C_FLUFF}%b${C_RESET}\n" "$*" >&2
   fi
}


log_verbose()
{
   if [ ! -z "${VERBOSE}" -a -z "${TERSE}" ]
   then
      printf "${C_VERBOSE}%b${C_RESET}\n" "$*" >&2
   fi
}


fail()
{
   log_error "$@"
   exit 1
}


is_root()
{
   if [ "$EUID" != "" ]
   then
      [ "$EUID" -eq 0 ]
   else
      [ "`id -u`" -eq 0 ]
   fi
}


sudo_if_needed()
{
   if [ -z "${SUDO}" ] || is_root
   then
      eval "$@"
   else
      command -v "${SUDO}" > /dev/null 2>&1
      if [ $? -ne 0 ]
      then
         fail "Install ${SUDO} or run as root"
      fi
      eval ${SUDO} "$@"
   fi
}


fetch_brew()
{
   case "${UNAME}" in
      Darwin)
         log_fluff "Installing OS X brew"

         ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)" || fail "ruby"
      ;;

      Linux)
         install_binary_if_missing "curl"
         install_binary_if_missing "python-setuptools"
         install_binary_if_missing "build-essential"
         install_binary_if_missing "ruby"

         log_fluff "Installing Linux brew"
         ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/linuxbrew/go/install)" || fail "ruby"
      ;;
   esac
}


install_with_brew()
{
   PATH="$PATH:/usr/local/bin" command -v "brew" > /dev/null 2>&1
   if [ $? -ne 0 ]
   then
      command -v "ruby" > /dev/null 2>&1
      if [ $? -ne 0 ]
      then
         fail "You need to install $1 manually from $2"
      fi

      fetch_brew
   fi

   log_info "Download $1 using brew"
   PATH="$PATH:/usr/local/bin" brew install "$1" || exit 1
}


install_binary_if_missing()
{
   if command -v "$1" > /dev/null 2>&1
   then
      return
   fi

   case "${UNAME}" in
      Darwin)
         install_with_brew "$@" || exit 1
      ;;

      Linux)
         if command -v "brew" > /dev/null 2>&1
         then
            install_with_brew "$@" || exit 1
         else
            if command -v "apt-get" > /dev/null 2>&1
            then
               log_info "You may get asked for your password to install $1"
               sudo_if_needed apt-get install "$1" || exit 1
            else
               if command -v "yum" > /dev/null 2>&1
               then
                  log_info "You may get asked for your password to install $1"
                  sudo_if_needed yum install "$1" || exit 1
               else
                  fail "You need to install $1 manually from $2"
               fi
            fi
         fi
      ;;

      FreeBSD)
         if command -v "pkg" > /dev/null 2>&1
         then
            log_info "You may get asked for your password to install $1"
            sudo_if_needed pkg install "$1" || exit 1
         else
            if command -v "pkg_add" > /dev/null 2>&1
            then
               log_info "You may get asked for your password to install $1"
               sudo_if_needed pkg_add -r "$1" || exit 1
            else
               fail "You need to install $1 manually from $2"
            fi
         fi
      ;;

      *)
         fail "You need to install $1 manually from $2"
      ;;
   esac
}


build_cmake()
{
   log_fluff "Build cmake..."

   install_binary_if_missing "curl" "https://curl.haxx.se/"
   install_binary_if_missing "${CXX_COMPILER}" "https://gcc.gnu.org/install/download.html"
   install_binary_if_missing "tar" "from somewhere"
   install_binary_if_missing "make" "from somewhere"

   mkdir "${SRC_DIR}" 2> /dev/null
   set -e
      cd "${SRC_DIR}"

         if [ -d "cmake-${CMAKE_PATCH_VERSION}" ]
         then
            rm -rf "cmake-${CMAKE_PATCH_VERSION}"
         fi
         if [ ! -f "cmake-${CMAKE_PATCH_VERSION}.tar.gz" ]
         then
            curl -k -L -O "https://cmake.org/files/v${CMAKE_VERSION}/cmake-${CMAKE_PATCH_VERSION}.tar.gz"
         fi

         tar xfz "cmake-${CMAKE_PATCH_VERSION}.tar.gz"
         cd "cmake-${CMAKE_PATCH_VERSION}"
         ./configure "--prefix=${PREFIX}"
         ${MAKE} install || exit 1

         hash -r  # apparently needed...
      cd "${OWD}"
   set +e
}


check_cmake_version()
{
   local major
   local minor
   local version

   version="`cmake -version 2> /dev/null| awk '{ print $3 }'`"
   if [ -z "${version}" ]
   then
      log_fluff "The cmake is not installed."
      return 2
   fi

   major="`echo "${version}" | head -1 | cut -d. -f1`"
   if [ -z "${major}" ]
   then
      fail "Could not figure out where cmake is and what version it is."
   fi

   minor="`echo "${version}" | head -1 | cut -d. -f2`"
   if [ "${major}" -lt "${CMAKE_VERSION_MAJOR}" ] || [ "${major}" -eq "${CMAKE_VERSION_MAJOR}" -a "${minor}" -lt "${CMAKE_VERSION_MINOR}" ]
   then
      return 1
   fi

   return 0
}


check_and_build_cmake()
{
   if [ -z "${BUILD_CMAKE}" ]
   then
      install_binary_if_missing "cmake" "https://cmake.org/download/"
   fi

   check_cmake_version
   case $? in
      0)
         return
      ;;

      1)
         log_fluff "The cmake version is too old. cmake version ${CMAKE_VERSION} or better is required."
      ;;

      2)
         :
      ;;
   esac

   log_fluff "Let's build cmake from scratch"
   build_cmake || fail "build_cmake failed"
}


get_core_count()
{
   local count

    command -v "nproc" > /dev/null 2>&1
    if [ $? -ne 0 ]
    then
       command -v "sysctl" > /dev/null 2>&1
       if [ $? -ne 0 ]
       then
          log_fluff "can't figure out core count, assume 4"
       else
          count="`sysctl -n hw.ncpu`"
       fi
    else
       count="`nproc`"
    fi

    if [ -z "$count" ]
    then
       count=4
    fi
    echo $count
}


#
# Setup environment
#
setup_build_environment()
{
   local do_install
   local version
   local minor
   local major

   do_install="YES"

   #
   # make sure cmake and git and gcc are present (and in the path)
   # should check version
   # Set some defaults so stuff possibly just magically works.
   #
   case "${UNAME}" in
      MINGW*)
         log_fluff "Detected MinGW on Windows"
         PATH="$PATH:/c/Program Files/CMake/bin/cmake:/c/Program Files (x86)/Microsoft Visual Studio 14.0/VC/bin"

         install_binary_if_missing "nmake" "https://www.visualstudio.com/de-de/downloads/download-visual-studio-vs.aspx and then add the directory containing nmake to your %PATH%"

         CMAKE_GENERATOR="NMake Makefiles"
         MAKE=nmake.exe
         CXX_COMPILER=cl.exe
         C_COMPILER=cl.exe
      ;;

      *)
         log_fluff "Detected ${UNAME}"
         install_binary_if_missing "make" "somewhere"
         install_binary_if_missing "python" "https://www.python.org/downloads/release"

         CMAKE_GENERATOR="Unix Makefiles"
         MAKE=make
         MAKE_FLAGS="${MAKE_FLAGS} -j `get_core_count`"
      ;;
   esac

   check_and_build_cmake

   if [ "${CXX_COMPILER}" = "g++" ]
   then
      install_binary_if_missing "g++" "https://gcc.gnu.org/install/download.html"
   else
      if [ "${CXX_COMPILER}" = "clang++" ]
      then
         install_binary_if_missing "clang++" "http://clang.llvm.org/get_started.html"
      else
         install_binary_if_missing "${CXX_COMPILER}" "somewhere (cpp compiler)"
      fi
   fi

   if [ "${C_COMPILER}" = "gcc" ]
   then
      install_binary_if_missing "gcc" "https://gcc.gnu.org/install/download.html"
   else
      if [ "${C_COMPILER}" = "clang" ]
      then
         install_binary_if_missing "clang" "http://clang.llvm.org/get_started.html"
      else
         install_binary_if_missing "${C_COMPILER}" "somewhere (c compiler)"
      fi
   fi

   if [ ! -z "${MULLE_BUILD_LLDB}" ]
   then
      install_binary_if_missing "swig" "http://swig.org/"
      case "${UNAME}" in
         Linux)
            install_binary_if_missing "libedit-dev" "somewhere"
            install_binary_if_missing "ncurses-dev" "somewhere"
            ;;
      esac
   fi
}


_llvm_module_download()
{
   local name
   local archive
   local dst

   name="$1"
   archive="$2"
   dst="$3"

   local filename
   local extractname

   filename="`basename -- "${archive}"`"
   extractname="`basename -- "${filename}" .tar.xz`"

   if [ ! -f "${filename}" ]
   then
      curl -L -C- -o "_${filename}" "${archive}"  || fail "curl failed"
      tar tfJ "_${filename}" > /dev/null || fail "tar archive corrupt"
      mv "_${filename}" "${filename}"
   fi

   tar xfJ "${filename}" || fail "tar failed"
   mv "${extractname}" "${dst}/${name}"
}


download_llvm()
{
   if [ ! -d "${LLVM_DIR}" ]
   then
      log_fluff "Download llvm..."

      _llvm_module_download "llvm" "${LLVM_ARCHIVE}" "${SRC_DIR}"
   fi

   if [ -z "${NO_LIBCXX}" ]
   then
      if [ ! -d "${LLVM_DIR}/projects/libcxx" ]
      then
         log_fluff "Download libcxx..."

         _llvm_module_download "libcxx" "${LIBCXX_ARCHIVE}" "${LLVM_DIR}/projects"
      fi

      if [ ! -d "${LLVM_DIR}/projects/libcxxabi" ]
      then
         log_fluff "Download libcxxabi..."

         _llvm_module_download "libcxxabi" "${LIBCXXABI_ARCHIVE}" "${LLVM_DIR}/projects"
      fi
   fi
}


download_clang()
{
   log_fluff "Download mulle-clang..."

   if [ ! -d "${CLANG_DIR}" ]
   then
      if [ ! -f mulle-clang.tgz ]
      then
         curl -L -C- -o _mulle-clang.tgz "${CLANG_ARCHIVE}"  || fail "curl failed"
         tar tfz _mulle-clang.tgz > /dev/null || fail "tar archive corrupt"
         mv _mulle-clang.tgz mulle-clang.tgz
      fi

      tar xfz mulle-clang.tgz
      mkdir -p "`dirname -- "${CLANG_DIR}"`" 2> /dev/null
      mv mulle-clang-3.9.0  "${CLANG_DIR}"
   fi
}



#
# on Debian, llvm doesn't build properly with clang
# use gcc, which is the default compiler for cmake
#
_build_llvm()
{
   #
   # Build llvm
   #
   if [ ! -f "${LLVM_BUILD_DIR}/Makefile" ]
   then
      mkdir -p "${LLVM_BUILD_DIR}" 2> /dev/null

      set -e
         cd "${LLVM_BUILD_DIR}"
            cmake \
               -Wno-dev \
               -G "${CMAKE_GENERATOR}" \
               -DCMAKE_BUILD_TYPE="${LLVM_BUILD_TYPE}" \
               -DCMAKE_INSTALL_PREFIX="${MULLE_LLVM_INSTALL_PREFIX}" \
               -DLLVM_ENABLE_CXX1Y:BOOL=OFF \
               ${CMAKE_FLAGS} \
               "${BUILD_RELATIVE}/../${LLVM_DIR}"
         cd "${OWD}"
      set +e
   fi

   cd "${LLVM_BUILD_DIR}" || fail "build_llvm: ${LLVM_BUILD_DIR} missing"
   # hmm
      ${MAKE} ${MAKE_FLAGS} "$@" || fail "build_llvm: ${MAKE} failed"
   cd "${OWD}"
}


build_llvm()
{
   log_fluff "Build llvm..."

   _build_llvm "$@"
}


#
# on Debian, clang doesn't build properly with gcc
# use clang, if available (as CXX_COMPILER)
#
_build_clang()
{
   #
   # Build mulle-clang
   #
   if [ ! -f "${CLANG_BUILD_DIR}/Makefile" ]
   then
      mkdir -p "${CLANG_BUILD_DIR}" 2> /dev/null

      set -e
         cd "${CLANG_BUILD_DIR}"

            PATH="${LLVM_BIN_DIR}:$PATH"

            # cmake -DCMAKE_BUILD_TYPE=Debug "../${CLANG_DIR}"
            # try to build cmake with cmake
            CC="${C_COMPILER}" CXX="${CXX_COMPILER}" \
               cmake \
                  -Wno-dev \
                  -G "${CMAKE_GENERATOR}" \
                  -DCMAKE_BUILD_TYPE="${CLANG_BUILD_TYPE}" \
                  -DCMAKE_INSTALL_PREFIX="${MULLE_CLANG_INSTALL_PREFIX}" \
                  ${CMAKE_FLAGS} \
                  "${BUILD_RELATIVE}/../${CLANG_DIR}"
         cd "${OWD}"
      set +e
   fi

   cd "${CLANG_BUILD_DIR}" || fail "build_clang: ${CLANG_BUILD_DIR} missing"
      ${MAKE} ${MAKE_FLAGS} "$@" || fail "build_clang: ${MAKE} failed"
   cd "${OWD}"
}


build_clang()
{
   log_fluff "Build clang..."

   _build_clang "$@"
}



download_mulle_clang()
{
# try to download most problematic first
# instead of downloading llvm first for an hour...

   download_clang

# should check if llvm is installed, if yes
# check proper version and then use it
   if [ "${BUILD_LLVM}" != "NO" ]
   then
      download_llvm
   fi
}


build_mulle_clang()
{
   log_fluff "Build mulle-clang..."

# should check if llvm is installed, if yes
# check proper version and then use it
   if [ "${BUILD_LLVM}" != "NO" ]
   then
      if [ "${INSTALL_LLVM}" != "NO" ]
      then
         build_llvm install
      else
         build_llvm
      fi
   fi

   build_clang install
}


_build_mulle_clang()
{
# should check if llvm is installed, if yes
# check proper version and then use it
   if [ "${BUILD_LLVM}" != "NO" ]
   then
      if [ "${INSTALL_LLVM}" != "NO" ]
      then
         _build_llvm install
      else
         _build_llvm
      fi
   fi

   _build_clang install
}


install_executable()
{
   local  src
   local  dst
   local  dstname

   src="$1"
   dstname="$2"
   dstdir="${3:-${SYMLINK_PREFIX}/bin}"

   log_fluff "Create symbolic link ${dstdir}/${dstname}"

   if [ ! -w "${dstdir}" ]
   then
      sudo_if_needed mkdir -p "${dstdir}"
      sudo_if_needed ln -s -f "${src}" "${dstdir}/${dstname}"
   else
      ln -s -f "${src}" "${dstdir}/${dstname}"
   fi
}


install_mulle_clang_link()
{
   log_fluff "Install mulle-clang link..."

   if [ ! -f "${MULLE_CLANG_INSTALL_PREFIX}/bin/clang${EXE_EXTENSION}" ]
   then
      fail "download and build mulle-clang with
   ./install-mulle-clang.sh
before you can install"
   fi

   install_executable "${MULLE_CLANG_INSTALL_PREFIX}/bin/clang${CLANG_SUFFIX}${EXE_EXTENSION}" mulle-clang${CLANG_SUFFIX}${EXE_EXTENSION}
}


uninstall_executable()
{
   local path

   path="${1}${EXE_EXTENSION}"

   if [ -e "${path}" ]
   then
      log_fluff "remove ${path}"

      if [ ! -w "${path}" ]
      then
         sudo_if_needed rm "${path}"
      else
         rm "${path}"
      fi
   else
      log_fluff "${path} is already gone"
   fi
}


uninstall_mulle_clang_link()
{
   local prefix

   log_fluff "Uninstall mulle-clang link..."

   prefix="${1:-${MULLE_CLANG_INSTALL_PREFIX}}"

   uninstall_executable "${prefix}/bin/mulle-clang${CLANG_SUFFIX}"
}


main()
{
   OWD="`pwd -P`"
   PREFIX="${OWD}"

   while [ $# -ne 0 ]
   do
      case "$1" in
         -t|--trace)
            set -x
         ;;

         -v|--verbose)
            FLUFF=
            VERBOSE="YES"
         ;;

         -vv|--very-verbose)
            FLUFF="YES"
            VERBOSE="YES"
         ;;

         -vvv|--very-verbose-with-settings)
            FLUFF="YES"
            VERBOSE="YES"
         ;;

         --build-cmake)
            BUILD_CMAKE="YES"
         ;;

         --prefix)
            [ $# -eq 1 ] && fail "missing argument to $1"
            shift
            PREFIX="$1"
         ;;

         --clang-prefix)
            [ $# -eq 1 ] && fail "missing argument to $1"
            shift
            MULLE_CLANG_INSTALL_PREFIX="$1"
         ;;

         --llvm-prefix)
            [ $# -eq 1 ] && fail "missing argument to $1"
            shift
            MULLE_LLVM_INSTALL_PREFIX="$1"
         ;;

         --symlink-prefix)
            [ $# -eq 1 ] && fail "missing argument to $1"
            shift
            SYMLINK_PREFIX="$1"
         ;;

         --no-libcxx)
            NO_LIBCXX="YES"
         ;;

         -*)
            echo "unknown option $1" >&2
            exit 1
         ;;

         *)
            break
         ;;
      esac

      shift
   done

   PATH="${PREFIX}/bin:$PATH"; export PATH

   MULLE_CLANG_INSTALL_PREFIX="${MULLE_CLANG_INSTALL_PREFIX:-${PREFIX}/mulle-clang/${MULLE_OBJC_VERSION}}"
   MULLE_LLVM_INSTALL_PREFIX="${MULLE_LLVM_INSTALL_PREFIX:-${PREFIX}}"

   COMMAND="${1:-default}"
   [ $# -eq 0 ] || shift


   # shouldn't thsis be CC /CXX ?
   C_COMPILER="${CC}"
   if [ -z "${C_COMPILER}" ]
   then
      C_COMPILER="`command -v "clang"`"
      if [ -z "${C_COMPILER}" ]
      then
         C_COMPILER="`command -v "gcc"`"
         if [ -z "${C_COMPILER}" ]
         then
            C_COMPILER="gcc"
         fi
      fi
      C_COMPILER="`basename "${C_COMPILER}"`"
   fi

   CXX_COMPILER="${CXX}"
   CXX_COMPILER="${CXX_COMPILER:-${C_COMPILER}++}"

   if [ "${CXX_COMPILER}" = "gcc++" ]
   then
      CXX_COMPILER="g++"
   fi

   #
   # these parameters are rarely needed
   #
   LLVM_BRANCH="release_${MULLE_OBJC_VERSION_BRANCH}"
   LLDB_BRANCH="${LLVM_BRANCH}"
   CLANG_BRANCH="${LLVM_BRANCH}"

   # "mulle_objcclang_${MULLE_OBJC_VERSION_BRANCH}"
   MULLE_CLANG_BRANCH="mulle_objclang_${MULLE_OBJC_VERSION_BRANCH}"
   MULLE_LLDB_BRANCH="${MULLE_CLANG_BRANCH}"

   #
   # it makes little sense to change these
   #
   SRC_DIR="src"

   LLVM_BUILD_TYPE="${LLVM_BUILD_TYPE:-Release}"
   LLDB_BUILD_TYPE="${LLDB_BUILD_TYPE:-Debug}"
   CLANG_BUILD_TYPE="${CLANG_BUILD_TYPE:-Release}"

   LLVM_DIR="${SRC_DIR}/llvm"
   CLANG_DIR="${SRC_DIR}/mulle-clang"

   BUILD_DIR="build"
   BUILD_RELATIVE=".."

   LLVM_BUILD_DIR="${BUILD_DIR}/llvm.d"
   CLANG_BUILD_DIR="${BUILD_DIR}/mulle-clang.d"

   # override this to use pre-installed llvm

   LLVM_BIN_DIR="${LLVM_BIN_DIR:-${LLVM_BUILD_DIR}/bin}"

   # blurb a little, this has some advantages

   log_info "MULLE_OBJC_VERSION=${MULLE_OBJC_VERSION}"
   log_info "MULLE_CLANG_INSTALL_PREFIX=${MULLE_CLANG_INSTALL_PREFIX}"
   log_info "SYMLINK_PREFIX=${SYMLINK_PREFIX}"

   setup_build_environment

   case "$COMMAND" in
      install)
         install_mulle_clang_link "$@"
      ;;

      default)
         download_mulle_clang
         build_mulle_clang
      ;;

      download)
         download_mulle_clang
      ;;

      build)
         build_mulle_clang
      ;;

      _build)
         _build_mulle_clang
      ;;

      uninstall)
         uninstall_mulle_clang_link
      ;;
   esac
}


environment_initialize
log_initialize
main "$@"
