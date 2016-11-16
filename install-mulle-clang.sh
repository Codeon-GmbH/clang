#! /bin/sh
#
# mulle-clang installer
# (c) 2016 Codeon GmbH, coded by Nat!
# BSD-3 License

# cmake/llvm cloning is very expensive, because the repos are huge

USE_CLONE="${USE_CLONE:-NO}"

# various versions
MULLE_OBJC_VERSION_BRANCH=39
MULLE_OBJC_VERSION="3.9.0"  # for opt

CMAKE_VERSION="3.5"
CMAKE_PATCH_VERSION="${CMAKE_VERSION}.2"

MULLE_BOOTSTRAP_MIN_MAJOR=2
MULLE_BOOTSTRAP_MIN_MINOR=3

MULLE_BUILD_MIN_MAJOR=0
MULLE_BUILD_MIN_MINOR=6

CLANG_ARCHIVE="https://github.com/Codeon-GmbH/mulle-clang/archive/3.9.0.tar.gz"
LLVM_ARCHIVE="http://www.llvm.org/releases/3.9.0/llvm-3.9.0.src.tar.xz"
LIBCXX_ARCHIVE="http://llvm.org/releases/3.9.0/libcxx-3.9.0.src.tar.xz"
LIBCXXABI_ARCHIVE="http://llvm.org/releases/3.9.0/libcxxabi-3.9.0.src.tar.xz"

#
#  this needs to be modified when hosting is not on mulle-kybernetik.com
#
#
MULLE_GITHUB_REPOSCHEME="https:/"
MULLE_GITHUB_REPOHOST="github.com"
MULLE_GITHUB_REPOPATHPREFIX="mulle-nat"
_MULLE_GITHUB_REPO="${MULLE_GITHUB_REPOSCHEME}/${MULLE_GITHUB_REPOHOST}/${MULLE_GITHUB_REPOPATHPREFIX}"
MULLE_GITHUB_REPO=${MULLE_GITHUB_REPO:-${_MULLE_GITHUB_REPO}}

#
#
CODEON_GITHUB_REPOSCHEME="https:/"
CODEON_GITHUB_REPOHOST="github.com"
CODEON_GITHUB_REPOPATHPREFIX="codeon-gmbh"
_CODEON_GITHUB_REPO="${CODEON_GITHUB_REPOSCHEME}/${CODEON_GITHUB_REPOHOST}/${CODEON_GITHUB_REPOPATHPREFIX}" # /${CODEON_GITHUB_REPOPATHPREFIX}"
CODEON_GITHUB_REPO=${CODEON_GITHUB_REPO:-${_CODEON_GITHUB_REPO}}


environment_initialize()
{
   #
   #
   #
   UNAME="`uname`"
   case "${UNAME}" in
      MINGW*)
         BOOTSTRAP_FLAGS="-v"
         CLANG_SUFFIX="-cl"
         EXE_EXTENSION=".exe"
         MULLE_CLANG_INSTALL_PREFIX="~/mulle-clang/${MULLE_OBJC_VERSION}"
         MULLE_LLDB_INSTALL_PREFIX="~/mulle-lldb/${MULLE_OBJC_VERSION}"
         SYMLINK_PREFIX="~"
         SUDO=
      ;;

      *)
         MULLE_CLANG_INSTALL_PREFIX="/opt/mulle-clang/${MULLE_OBJC_VERSION}"
         MULLE_LLDB_INSTALL_PREFIX="/opt/mulle-lldb/${MULLE_OBJC_VERSION}"
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
         install_binary_if_missing "git"
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

      cd "${OWD}"
   set +e
}


check_cmake_version()
{
   local major
   local minor
   local version

   version="`cmake -version | awk '{ print $3 }'`"
   major="`echo "${version}" | head -1 | cut -d. -f1`"
   if [ -z "${major}" ]
   then
       fail "Could not figure out where cmake is and what version it is."
   fi

   minor="`echo "${version}" | head -1 | cut -d. -f2`"
   if [ "${major}" -lt 3 ] || [ "${major}" -eq 3 -a "${minor}" -lt 4 ]
   then
      log_fluff "The cmake version is too old. cmake version 3.4 or better is required."
      log_fluff "Let's build cmake from scratch"

      build_cmake || fail "build_cmake failed"
   fi
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

    if [ "$count" = "" ]
    then
       count=4
    fi
    echo $count
}


ensure_proper_git_branch()
{
   local src
   local vendor_branch
   local build_dir
   local branch

   src="$1"
   [ $# -eq 0 ] || shift
   [ -z "${src}" ] && fail "ensure_proper_git_branch: src not set"

   vendor_branch="$1"
   [ $# -eq 0 ] || shift
   [ -z "${vendor_branch}" ] && fail "ensure_proper_git_branch: vendor_branch not set"

   build_dir="$1"
   [ $# -eq 0 ] || shift
   [ -z "${build_dir}" ] && fail "ensure_proper_git_branch: build_dir not set"

   branch="$1"
   [ $# -eq 0 ] || shift

   local curr_branch
   local owd
   local checkout_branch

   checkout_branch="${branch}"
   if [ -z "${checkout_branch}" ]
   then
      checkout_branch="${vendor_branch}"
   fi

   set -e

      owd="`pwd -P`"

      cd "$src" || exit 1

         curr_branch="`git rev-parse --abbrev-ref HEAD`"

         if [ "${curr_branch}" != "${checkout_branch}" ]
         then
            git reset --hard
            git fetch origin "${vendor_branch}"
            if [ ! -z "$branch" ]
            then
               git fetch mulle "${branch}"
            fi
            git checkout -f "${checkout_branch}"

            [ -d "${OWD}/${build_dir}" ] && rm -rf "${OWD}/${build_dir}"

            curr_branch="`git rev-parse --abbrev-ref HEAD`"
            if [ "${curr_branch}" != "${checkout_branch}" ]
            then
               fail "You lost again"
            fi
         fi

      cd "${OWD}"

   set +e
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

   install_binary_if_missing "git" "https://git-scm.com/downloads"

   do_install="YES"

   #
   # install a mulle-bootstrap copy in ./bin if needed
   #
   local exepath

   exepath="`command -v "mulle-bootstrap" 2> /dev/null`"
   if [ $? -eq 0 ]
   then
      do_install="NO"
      version="`mulle-bootstrap version`"
      [ -z "${version}" ] && fail "mulle-bootstrap did not provide a version"

      major="`echo "${version}" | head -1 | cut -d. -f1`"
      if [ "${major}" -le "${MULLE_BOOTSTRAP_MIN_MAJOR}" ]
      then
         do_install="YES"
         if [ "${major}" -eq "${MULLE_BOOTSTRAP_MIN_MAJOR}" ]
         then
            minor="`echo "${version}" | head -1 | cut -d. -f2`"
            if [ "${minor}" -ge "${MULLE_BOOTSTRAP_MIN_MINOR}" ]
            then
               do_install="NO"
            fi
         fi
      fi

      if [ "${do_install}" = "YES" -a "${exepath}" != "${PREFIX}/bin/mulle-bootstrap" ]
      then
         # avoid having two around, possibly
         fail "your mulle-bootstrap is too old, please upgrade to ${MULLE_BOOTSTRAP_MIN_MAJOR}.${MULLE_BOOTSTRAP_MIN_MINOR} minimum"
      fi
   fi

   if [ "${do_install}" = "YES" ]
   then
      mkdir "${SRC_DIR}" 2> /dev/null
      set -e
         if [ ! -d "${SRC_DIR}/mulle-bootstrap" ]
         then
            log_fluff "Download mulle-bootstrap"
            git clone "${MULLE_GITHUB_REPO}/mulle-bootstrap" "${SRC_DIR}/mulle-bootstrap"
         else
            ( cd "${SRC_DIR}/mulle-bootstrap" ; git pull "${MULLE_GITHUB_REPO}/mulle-bootstrap" )
         fi

         cd "${SRC_DIR}/mulle-bootstrap"
            ./install.sh "${PREFIX}"
         cd "${OWD}"

      set +e
   fi


   #
   # install a mulle-build copy in ./bin if needed
   #
   do_install="YES"

   exepath="`command -v "mulle-build" 2> /dev/null`"
   if [ $? -eq 0 ]
   then
      do_install="NO"
      version="`mulle-build --version`"
      [ -z "${version}" ] && fail "mulle-build did not provide a version"

      major="`echo "${version}" | head -1 | cut -d. -f1`"
      if [ "${major}" -le "${MULLE_BUILD_MIN_MAJOR}" ]
      then
         do_install="YES"
         if [ "${major}" -eq "${MULLE_BUILD_MIN_MAJOR}" ]
         then
            minor="`echo "${version}" | head -1 | cut -d. -f2`"
            if [ "${minor}" -ge "${MULLE_BUILD_MIN_MINOR}" ]
            then
               do_install="NO"
            fi
         fi
      fi

      if [ "${do_install}" = "YES" -a "${exepath}" != "${PREFIX}/bin/mulle-build" ]
      then
         # avoid having two around, possibly
         fail "your mulle-build is too old, please upgrade to ${MULLE_BUILD_MIN_MAJOR}.${MULLE_BUILD_MIN_MINOR} minimum"
      fi
   fi

   if [ "${do_install}" = "YES" ]
   then
      mkdir "${SRC_DIR}" 2> /dev/null
      set -e
         if [ ! -d "${SRC_DIR}/mulle-build" ]
         then
            log_fluff "Download mulle-build"
            git clone "${MULLE_GITHUB_REPO}/mulle-build" "${SRC_DIR}/mulle-build"
         else
            ( cd "${SRC_DIR}/mulle-build" ; git pull "${MULLE_GITHUB_REPO}/mulle-build" )
         fi

         cd "${SRC_DIR}/mulle-build"
            ./install.sh "${PREFIX}"
         cd "${OWD}"

      set +e
   fi

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
         install_binary_if_missing "cmake" "https://cmake.org/download/"
         CMAKE_GENERATOR="NMake Makefiles"
         MAKE=nmake.exe
         CXX_COMPILER=cl.exe
         C_COMPILER=cl.exe
      ;;

      *)
         log_fluff "Detected ${UNAME}"
         install_binary_if_missing "make" "somewhere"
         install_binary_if_missing "python" "https://www.python.org/downloads/release"
         install_binary_if_missing "cmake" "https://cmake.org/download/"
         CMAKE_GENERATOR="Unix Makefiles"
         MAKE=make
         MAKE_FLAGS="${MAKE_FLAGS} -j `get_core_count`"
      ;;
   esac

   check_cmake_version

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

      case "${USE_CLONE}" in
         YES)
            log_fluff "Download llvm from github mirror"
            git clone -b "${LLVM_BRANCH}" "https://github.com/llvm-mirror/llvm.git" "${LLVM_DIR}"  || fail "llvm git clone failed"
         ;;

         ""|*)
            _llvm_module_download "llvm" "${LLVM_ARCHIVE}" "${SRC_DIR}"
         ;;
      esac
   fi


   if [ ! -d "${LLVM_DIR}/projects/libcxx" ]
   then
      log_fluff "Download libcxx..."

      case "${USE_CLONE}" in
         YES)
            (cd "${LLVM_DIR}/projects"; git clone -b "${LLVM_BRANCH}" "https://github.com/llvm-mirror/libcxx.git"  || fail "libcxx git clone failed")
         ;;

         ""|*)
            _llvm_module_download "libcxx" "${LIBCXX_ARCHIVE}" "${LLVM_DIR}/projects"
         ;;
      esac
   fi

   if [ ! -d "${LLVM_DIR}/projects/libcxxabi" ]
   then
      log_fluff "Download libcxxabi..."

      case "${USE_CLONE}" in
         YES)
            (cd "${LLVM_DIR}/projects"; git clone -b "${LLVM_BRANCH}" "https://github.com/llvm-mirror/libcxxabi.git"  || fail "libcxxabi git clone failed")
         ;;

         ""|*)
            _llvm_module_download "libcxxabi" "${LIBCXXABI_ARCHIVE}" "${LLVM_DIR}/projects"
         ;;

      esac
   fi
}




download_clang()
{
   log_fluff "Download mulle-clang..."

   if [ ! -d "${CLANG_DIR}" ]
   then
      case "${USE_CLONE}" in
         YES)
            git clone -b "${MULLE_CLANG_BRANCH}" "${CODEON_GITHUB_REPO}/mulle-clang.git" "${CLANG_DIR}"  || fail "git clone failed"
         ;;

         ""|*)
            if [ ! -f mulle-clang.tgz ]
            then
               curl -L -C- -o _mulle-clang.tgz "${CLANG_ARCHIVE}"  || fail "curl failed"
               tar tfz _mulle-clang.tgz > /dev/null || fail "tar archive corrupt"
               mv _mulle-clang.tgz mulle-clang.tgz
            fi

            tar xfz mulle-clang.tgz
            mkdir -p "`dirname -- "${CLANG_DIR}"`" 2> /dev/null
            mv mulle-clang-3.9.0  "${CLANG_DIR}"
         ;;
      esac
   fi
}


download_lldb()
{
   log_fluff "Download mulle-lldb..."

   if [ ! -d "${LLDB_DIR}" ]
   then
      git clone -b "${MULLE_LLDB_BRANCH}" "${CODEON_GITHUB_REPO}/mulle-lldb.git" "${LLDB_DIR}"  || fail "git clone failed"
   fi
}


update_llvm()
{
   log_fluff "Update llvm..."

   case "${USE_CLONE}" in
      YES)
         (
            cd "${LLVM_DIR}" &&
            git fetch origin "${LLVM_BRANCH}" &&
            git reset --hard "origin/${LLVM_BRANCH}"
         )  || fail "git fetch failed"
      ;;
   esac
}


update_clang()
{
   log_fluff "Update clang..."

   case "${USE_CLONE}" in
      YES)
         (
            cd "${CLANG_DIR}" &&
            git fetch origin "${MULLE_CLANG_BRANCH}"
         )  || fail "git fetch failed"
      ;;
   esac
}


update_lldb()
{
   log_fluff "Update lldb..."

   ( cd "${LLDB_DIR}" ;  git fetch origin "${MULLE_LLDB_BRANCH}" ) || fail "git fetch failed"
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
               -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
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

   if [ "${USE_CLONE}" = "YES" ]
   then
      ensure_proper_git_branch "${LLVM_DIR}" "${LLVM_BRANCH}" "${LLVM_BUILD_DIR}"
   fi

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

   if [ "${USE_CLONE}" = "YES" ]
   then
      ensure_proper_git_branch "${CLANG_DIR}" "${CLANG_BRANCH}" "${CLANG_BUILD_DIR}" "${MULLE_CLANG_BRANCH}"
   fi

   _build_clang "$@"
}


_build_lldb()
{
   #
   # Build mulle-clang
   #
   if [ ! -f "${LLDB_BUILD_DIR}/Makefile" ]
   then
      mkdir -p "${LLDB_BUILD_DIR}" 2> /dev/null

      set -e
         cd "${LLDB_BUILD_DIR}"

            PATH="${LLVM_BIN_DIR}:${OWD}/${CLANG_BUILD_DIR}/bin:$PATH"

            CC=clang CXX=clang++ \
               cmake \
                  -Wno-dev \
                  -G "${CMAKE_GENERATOR}" \
                  -DCMAKE_BUILD_TYPE="${LLDB_BUILD_TYPE}" \
                  -DCMAKE_INSTALL_PREFIX="${MULLE_LLDB_INSTALL_PREFIX}" \
                  -DLLDB_PATH_TO_CLANG_BUILD="${OWD}/${CLANG_BUILD_DIR}" \
                  -DLLDB_PATH_TO_CLANG_SOURCE="${OWD}/${CLANG_DIR}" \
                  -DLLDB_PATH_TO_LLVM_BUILD="${OWD}/${LLVM_BUILD_DIR}" \
                  -DLLDB_PATH_TO_LLVM_SOURCE="${OWD}/${LLVM_DIR}" \
                  -DLLDB_DISABLE_PYTHON=1 \
                  -DLLDB_DISABLE_LIBEDIT=0 \
                  ${CMAKE_FLAGS} \
                  "${BUILD_RELATIVE}/../${LLDB_DIR}"
         cd "${OWD}"
      set +e
   fi

   cd "${LLDB_BUILD_DIR}" || fail "build_clang: ${LLDB_BUILD_DIR} missing"
      ${MAKE} ${MAKE_FLAGS} "$@" || fail "build_lldb: ${MAKE} failed"
   cd "${OWD}"
}


build_lldb()
{
   log_fluff "Build lldb..."

   if [ "${USE_CLONE}" = "YES" ]
   then
      ensure_proper_git_branch "${LLDB_DIR}" "${LLDB_BRANCH}" "${LLDB_BUILD_DIR}" "${MULLE_LLDB_BRANCH}"
   fi
   _build_lldb "$@"
}


download_mulle_clang()
{
   install_binary_if_missing "git" "https://git-scm.com/downloads"

# try to download most problematic first
# instead of downloading llvm first for an hour...

   download_clang

# should check if llvm is installed, if yes
# check proper version and then use it
   if [ "${BUILD_LLVM}" != "NO" ]
   then
      download_llvm
   fi

   if [ ! -z "${MULLE_BUILD_LLDB}" ]
   then
      download_lldb
   fi
}


update_mulle_clang()
{
   log_fluff "Update mulle-clang..."

# should check if llvm is installed, if yes
# check proper version and then use it
   if [ "${BUILD_LLVM}" != "NO" ]
   then
      update_llvm
   fi

   update_clang
   if [ ! -z "${MULLE_BUILD_LLDB}" ]
   then
      update_lldb
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
   if [ ! -z "${MULLE_BUILD_LLDB}" ]
   then
      build_lldb install
   fi
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
   if [ ! -z "${MULLE_BUILD_LLDB}" ]
   then
      _build_lldb install
   fi
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


install_mulle_lldb_link()
{
   log_fluff "Install mulle-lldb link..."

   if [ ! -f "${MULLE_LLDB_INSTALL_PREFIX}/bin/lldb${EXE_EXTENSION}" ]
   then
      fail "download and build mulle-lldb first with
   ./install-mulle-clang.sh --lldb
before you can install"
   fi

   install_executable "${MULLE_LLDB_INSTALL_PREFIX}/bin/lldb${EXE_EXTENSION}" mulle-lldb${EXE_EXTENSION}
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


uninstall_mulle_lldb_link()
{
   local prefix

   log_fluff "Uninstall mulle-lldb link..."

   prefix="${1:-${MULLE_CLANG_INSTALL_PREFIX}}"

   uninstall_executable "${prefix}/bin/mulle-lldb"
}


main()
{
   OWD="`pwd -P`"
   PATH="${OWD}/bin:$PATH"; export PATH

   while [ $# -ne 0 ]
   do
      case "$1" in
         --lldb)
            MULLE_BUILD_LLDB="YES"
         ;;

         -t|--trace)
            BOOTSTRAP_FLAGS="`concat "${BOOTSTRAP_FLAGS}" "$1"`"
            set -x
         ;;

         -v|--verbose)
            BOOTSTRAP_FLAGS="`concat "${BOOTSTRAP_FLAGS}" "$1"`"
            FLUFF=
            VERBOSE="YES"
         ;;

         -vv|--very-verbose)
            BOOTSTRAP_FLAGS="`concat "${BOOTSTRAP_FLAGS}" "$1"`"
            FLUFF="YES"
            VERBOSE="YES"
            MULLE_EXECUTOR_TRACE="YES"
         ;;

         -vvv|--very-verbose-with-settings)
            BOOTSTRAP_FLAGS="`concat "${BOOTSTRAP_FLAGS}" "$1"`"
            FLUFF="YES"
            VERBOSE="YES"
            MULLE_EXECUTOR_TRACE="YES"
         ;;

         # known mulle-bootstrap flags
         -f|-D*|-V|--verbose-build)
            BOOTSTRAP_FLAGS="`concat "${BOOTSTRAP_FLAGS}" "$1"`"
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

   COMMAND="${1:-default}"
   [ $# -eq 0 ] || shift


   SYMLINK_PREFIX="${1:-${SYMLINK_PREFIX}}"
   [ $# -eq 0 ] || shift

   MULLE_CLANG_INSTALL_PREFIX="${1:-${MULLE_CLANG_INSTALL_PREFIX}}"
   [ $# -eq 0 ] || shift

   MULLE_LLDB_INSTALL_PREFIX="${1:-${MULLE_LLDB_INSTALL_PREFIX}}"
   [ $# -eq 0 ] || shift

   # shouldn't thsis be CC /CXX ?
   C_COMPILER="${1:-${CC}}"
   [ $# -eq 0 ] || shift

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

   CXX_COMPILER="${1:-${CXX}}"
   [ $# -eq 0 ] || shift
   CXX_COMPILER="${CXX_COMPILER:-${C_COMPILER}++}"

   if [ "${CXX_COMPILER}" = "gcc++" ]
   then
      CXX_COMPILER="g++"
   fi

   #
   # these parameters are rarely needed
   #
   LLVM_BRANCH="${1:-release_${MULLE_OBJC_VERSION_BRANCH}}"
   [ $# -eq 0 ] || shift
   LLDB_BRANCH="${LLVM_BRANCH}"
   CLANG_BRANCH="${LLVM_BRANCH}"

   # "mulle_objcclang_${MULLE_OBJC_VERSION_BRANCH}"
   MULLE_CLANG_BRANCH="${1:-mulle_objclang_${MULLE_OBJC_VERSION_BRANCH}}"
   [ $# -eq 0 ] || shift
   MULLE_LLDB_BRANCH="${1:-${MULLE_CLANG_BRANCH}}"
   [ $# -eq 0 ] || shift

   #
   # it makes little sense to change these
   #
   SRC_DIR="src"

   LLVM_BUILD_TYPE="${LLVM_BUILD_TYPE:-Release}"
   LLDB_BUILD_TYPE="${LLDB_BUILD_TYPE:-Debug}"
   CLANG_BUILD_TYPE="${CLANG_BUILD_TYPE:-Release}"

   LLDB_DIR="${SRC_DIR}/mulle-lldb"
   LLVM_DIR="${SRC_DIR}/llvm"
   CLANG_DIR="${SRC_DIR}/mulle-clang"

   BUILD_DIR="build"
   BUILD_RELATIVE=".."

   LLDB_BUILD_DIR="${BUILD_DIR}/mulle-lldb.d"
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
         [ ! -z "${MULLE_BUILD_LLDB}" ] && install_mulle_lldb_link "$@"
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

      update-only)
         update_mulle_clang
      ;;

      update)
         update_mulle_clang
         build_mulle_clang
      ;;

      uninstall)
         uninstall_mulle_clang_link
         [ ! -z "${MULLE_BUILD_LLDB}" ] && uninstall_mulle_lldb_link
      ;;
   esac
}


environment_initialize
log_initialize
main "$@"
