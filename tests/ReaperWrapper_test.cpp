#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "RustRefactorWrapper.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;
    using json = nlohmann::json;

    spdlog::set_level(spdlog::level::debug);

    std::string seededRustStr = R"(
use ::libc;
extern "C" {
    #[c2rust::src_loc = "16988:14"]
    pub fn malloc(_: libc::c_ulong) -> *mut libc::c_void;
    #[c2rust::src_loc = "16991:14"]
    pub fn calloc(_: libc::c_ulong, _: libc::c_ulong) -> *mut libc::c_void;
    #[c2rust::src_loc = "17003:13"]
    pub fn free(_: *mut libc::c_void);
}
#[c2rust::src_loc = "1004:1"]
pub type uInt = libc::c_uint;
#[c2rust::src_loc = "1005:1"]
pub type uLong = libc::c_ulong;
#[c2rust::src_loc = "1022:4"]
pub type voidpf = *mut libc::c_void;
#[c2rust::src_loc = "5120:1"]
pub type __off_t = libc::c_long;
#[c2rust::src_loc = "5282:1"]
pub type off_t = __off_t;
#[no_mangle]
#[c2rust::src_loc = "22929:22"]
pub static mut z_errmsg: [*mut libc::c_char; 10] = [
    b"need dictionary\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"stream end\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"file error\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"stream error\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"data error\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"insufficient memory\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"buffer error\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"incompatible version\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
    b"\0" as *const u8 as *const libc::c_char as *mut libc::c_char,
];
#[no_mangle]
#[c2rust::src_loc = "22943:1"]
pub unsafe extern "C" fn zlibVersion() -> *const libc::c_char {
    return (*if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"cuLnColBegin\":\"22944:12\",\"cuLnColEnd\":\"22944:24\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:28:12\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:28:24\",\"locRefBegin\":\"/home/hurrypeng/zlib/zlib.h:40:9\",\"name\":\"ZLIB_VERSION\",\"premise\":\"\",\"seedType\":\"invocation\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        &*::core::mem::transmute::<&[u8; 6], &[libc::c_char; 6]>(b"1.3.1\0")
            as *const [libc::c_char; 6]
    } else {
        0 as *const [libc::c_char; 6]
    })
        .as_ptr();
}
#[no_mangle]
#[c2rust::src_loc = "22947:1"]
pub unsafe extern "C" fn zlibCompileFlags() -> uLong {
    let mut flags: uLong = 0;
    flags = 0 as libc::c_int as uLong;
    match ::core::mem::size_of::<uInt>() as libc::c_ulong as libc::c_int {
        2 => {}
        4 => {
            flags = flags.wrapping_add(1 as libc::c_int as uLong);
        }
        8 => {
            flags = flags.wrapping_add(2 as libc::c_int as uLong);
        }
        _ => {
            flags = flags.wrapping_add(3 as libc::c_int as uLong);
        }
    }
    match ::core::mem::size_of::<uLong>() as libc::c_ulong as libc::c_int {
        2 => {}
        4 => {
            flags = flags.wrapping_add(((1 as libc::c_int) << 2 as libc::c_int) as uLong);
        }
        8 => {
            flags = flags.wrapping_add(((2 as libc::c_int) << 2 as libc::c_int) as uLong);
        }
        _ => {
            flags = flags.wrapping_add(((3 as libc::c_int) << 2 as libc::c_int) as uLong);
        }
    }
    match ::core::mem::size_of::<voidpf>() as libc::c_ulong as libc::c_int {
        2 => {}
        4 => {
            flags = flags.wrapping_add(((1 as libc::c_int) << 4 as libc::c_int) as uLong);
        }
        8 => {
            flags = flags.wrapping_add(((2 as libc::c_int) << 4 as libc::c_int) as uLong);
        }
        _ => {
            flags = flags.wrapping_add(((3 as libc::c_int) << 4 as libc::c_int) as uLong);
        }
    }
    match ::core::mem::size_of::<off_t>() as libc::c_ulong as libc::c_int {
        2 => {}
        4 => {
            flags = flags.wrapping_add(((1 as libc::c_int) << 6 as libc::c_int) as uLong);
        }
        8 => {
            flags = flags.wrapping_add(((2 as libc::c_int) << 6 as libc::c_int) as uLong);
        }
        _ => {
            flags = flags.wrapping_add(((3 as libc::c_int) << 6 as libc::c_int) as uLong);
        }
    }
    *(b"{\"astKind\":\"Stmt\",\"begin\":true,\"cuLnColBegin\":\"23027:5\",\"cuLnColEnd\":\"23027:23\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:96:5\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:96:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:89:1\",\"mergedVariants\":[\"/home/hurrypeng/zlib/zutil.c:96:5\"],\"premise\":\"not(feature = \\\"defNO_vsnprintf\\\")\",\"seedType\":\"conditional\"}\0"
        as *const u8 as *const libc::c_char);
    *(b"{\"astKind\":\"Stmt\",\"begin\":true,\"cuLnColBegin\":\"23027:5\",\"cuLnColEnd\":\"23027:23\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:96:5\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:96:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:95:1\",\"mergedVariants\":[\"/home/hurrypeng/zlib/zutil.c:96:5\"],\"premise\":\"all(feature = \\\"defHAS_vsnprintf_void\\\", not(feature = \\\"defNO_vsnprintf\\\"))\",\"seedType\":\"conditional\"}\0"
        as *const u8 as *const libc::c_char);
    flags = flags.wrapping_add(((1 as libc::c_long) << 26 as libc::c_int) as uLong);
    *(b"{\"astKind\":\"Stmt\",\"begin\":false,\"cuLnColBegin\":\"23027:5\",\"cuLnColEnd\":\"23027:23\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:96:5\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:96:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:95:1\",\"mergedVariants\":[\"/home/hurrypeng/zlib/zutil.c:96:5\"],\"premise\":\"all(feature = \\\"defHAS_vsnprintf_void\\\", not(feature = \\\"defNO_vsnprintf\\\"))\",\"seedType\":\"conditional\"}\0"
        as *const u8 as *const libc::c_char);
    *(b"{\"astKind\":\"Stmt\",\"begin\":false,\"cuLnColBegin\":\"23027:5\",\"cuLnColEnd\":\"23027:23\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:96:5\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:96:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:89:1\",\"mergedVariants\":[\"/home/hurrypeng/zlib/zutil.c:96:5\"],\"premise\":\"not(feature = \\\"defNO_vsnprintf\\\")\",\"seedType\":\"conditional\"}\0"
        as *const u8 as *const libc::c_char);
    return flags;
}
#[no_mangle]
#[c2rust::src_loc = "23076:1"]
pub unsafe extern "C" fn zError(mut err: libc::c_int) -> *const libc::c_char {
    return *if *(b"{\"argNames\":[\"err\"],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"23077:12\",\"cuLnColEnd\":\"23077:24\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:132:12\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:132:24\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.h:59:9\",\"name\":\"ERR_MSG\",\"premise\":\"\",\"seedType\":\"invocation\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        &*z_errmsg
            .as_ptr()
            .offset(
                (if *(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"23077:20\",\"cuLnColEnd\":\"23077:23\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:132:20\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:132:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:132:12\",\"name\":\"err\",\"premise\":\"\",\"seedType\":\"invocation\"}\0"
                    as *const u8 as *const libc::c_char) as libc::c_int != 0
                {
                    &mut err as *mut libc::c_int
                } else {
                    0 as *mut libc::c_int
                }) < -(6 as libc::c_int)
                    || *(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"23077:20\",\"cuLnColEnd\":\"23077:23\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:132:20\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:132:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:132:12\",\"name\":\"err\",\"premise\":\"\",\"seedType\":\"invocation\"}\0"
                        as *const u8 as *const libc::c_char) as libc::c_int != 0
                    {
                        &mut err as *mut libc::c_int
                    } else {
                        0 as *mut libc::c_int
                    }) > 2 as libc::c_int
                {
                    9 as libc::c_int
                } else {
                    2 as libc::c_int
                        - *(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"23077:20\",\"cuLnColEnd\":\"23077:23\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/zlib/zutil.c:132:20\",\"locEnd\":\"/home/hurrypeng/zlib/zutil.c:132:23\",\"locRefBegin\":\"/home/hurrypeng/zlib/zutil.c:132:12\",\"name\":\"err\",\"premise\":\"\",\"seedType\":\"invocation\"}\0"
                            as *const u8 as *const libc::c_char) as libc::c_int != 0
                        {
                            &mut err
                        } else {
                            0 as *mut libc::c_int
                        })
                }) as isize,
            ) as *const *mut libc::c_char
    } else {
        0 as *const *mut libc::c_char
    };
}
#[no_mangle]
#[c2rust::src_loc = "23248:1"]
pub unsafe extern "C" fn zcalloc(
    mut opaque: voidpf,
    mut items: libc::c_uint,
    mut size: libc::c_uint,
) -> voidpf {
    return if ::core::mem::size_of::<uInt>() as libc::c_ulong > 2 as libc::c_int as libc::c_ulong {
        malloc(items.wrapping_mul(size) as libc::c_ulong)
    } else {
        calloc(items as libc::c_ulong, size as libc::c_ulong)
    };
}
#[no_mangle]
#[c2rust::src_loc = "23254:1"]
pub unsafe extern "C" fn zcfree(mut opaque: voidpf, mut ptr: voidpf) {
    free(ptr);
}


)";

    std::string rustStr = RustRefactorWrapper::runReaper(seededRustStr);
    if (rustStr == seededRustStr)
    {
        std::cerr << "Reaper did not modify the input Rust code." << std::endl;
        return 1;
    }
    std::cout << "Reaper output:\n" << rustStr << std::endl;

    return 0;
}
