#! /bin/sh

fail()
{
   echo "$*" >&2
   exit 1
}


get_mulle_clang_version()
{
   local src="$1"

   if [ ! -d "${src}" ]
   then
      fail "mulle-clang not downloaded yet"
   fi

   if [ ! -f "${src}/install-mulle-clang.sh" ]
   then
      fail "No MULLE_CLANG_VERSION version found in \"${src}\""
   fi

   grep MULLE_CLANG_VERSION "${src}/install-mulle-clang.sh" | head -1 | sed 's/.*\"\(.*\)\"/\1/'
}


get_runtime_load_version()
{
   local src="$1"

   if [ ! -f "${src}/lib/CodeGen/CGObjCMulleRuntime.cpp" ]
   then
      fail "\"CGObjCMulleRuntime.cpp\" not found in \"${src}/lib/CodeGen\""
   fi

   grep COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION "${src}/lib/CodeGen/CGObjCMulleRuntime.cpp" \
    | head -1 \
    | awk '{ print $3 }'
}


get_clang_vendor()
{
   local src

   src="$1"

   local compiler_version
   local runtime_load_version

   compiler_version="`get_mulle_clang_version "${src}"`"
   if [ -z "${compiler_version}" ]
   then
      fail "Could not determine mulle-clang version"
   fi

   runtime_load_version="`get_runtime_load_version "${src}"`"
   if [ -z "${runtime_load_version}" ]
   then
      fail "Could not determine runtime load version"
   fi

   echo "mulle-clang ${compiler_version} (runtime-load-version: `eval echo ${runtime_load_version}`)"
}


BUILD_DIR="build"
PREFIX="${PWD}"

# different builds for OS Versions on OS X
case "`uname -s`" in
   Darwin)
      osxversion="`sw_vers -productVersion | cut -d. -f 1-2`"
      BUILD_DIR="build-${osxversion}"
      PREFIX="${OWD}/${osxversion}"
   ;;
esac



PATH=`pwd`/${BUILD_DIR}/llvm.d/bin:$PATH

CONFIGURATION="${1:-Release}"
[ $# -ne 0 ] && shift
DIR="${1:-${BUILD_DIR}/mulle-clang-xcode.d}"
[ $# -ne 0 ] && shift
SRC="${1:-src/mulle-clang}"
[ $# -ne 0 ] && shift


CLANG_VENDOR="`get_clang_vendor "${SRC}"`"
MULLE_CLANG_INSTALL_PREFIX="$PREFIX"

mkdir -p "${DIR}" > /dev/null
(
   cd "${DIR}"

   cmake -DCMAKE_OSX_SYSROOT=`xcrun --show-sdk-path` \
         -DWITH_POLLY=OFF \
         -DCLANG_VENDOR="${CLANG_VENDOR}" \
         -DCMAKE_OSX_DEPLOYMENT_TARGET=10.10 \
         -DCMAKE_INSTALL_PREFIX="${MULLE_CLANG_INSTALL_PREFIX}" \
         -DCMAKE_BUILD_TYPE="${CONFIGURATION}" -G "Xcode" "../../${SRC}"

   if [ $? -ne 0 ]
   then
      exit 1
   fi
) || exit 1

echo "ln -sf `pwd -P`/${BUILD_DIR}/mulle-clang-xcode.d/${CONFIGURATION}/bin/clang /usr/local/bin/mulle-clang"
ln -sf "`pwd -P`/${BUILD_DIR}/mulle-clang-xcode.d/${CONFIGURATION}/bin/clang" "/usr/local/bin/mulle-clang"

echo "ln -sf `pwd -P`/${BUILD_DIR}/mulle-clang-xcode.d/bin/scan-build /usr/local/bin/mulle-scan-build"
ln -sf "`pwd -P`/${BUILD_DIR}/mulle-clang-xcode.d/bin/scan-build" "/usr/local/bin/mulle-scan-build"

echo ln -sf "${DIR}/Clang.xcodeproj"
ln -sf "${DIR}/Clang.xcodeproj"
