#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "ReaperWrapper.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;
    using json = nlohmann::json;

    spdlog::set_level(spdlog::level::debug);

    std::string seededRustStr = R"(
#![allow(
    dead_code,
    mutable_transmutes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    unused_assignments,
    unused_mut
)]
#![register_tool(c2rust)]
#![feature(register_tool)]
extern "C" {
    #[c2rust::src_loc = "385:1"]
    pub fn expf(_: libc::c_float) -> libc::c_float;
    #[c2rust::src_loc = "387:1"]
    pub fn expm1f(_: libc::c_float) -> libc::c_float;
    #[c2rust::src_loc = "402:1"]
    pub fn fabsf(_: libc::c_float) -> libc::c_float;
    #[c2rust::src_loc = "538:1"]
    pub fn __signbitf(_: libc::c_float) -> libc::c_int;
    #[c2rust::src_loc = "539:1"]
    pub fn __signbitd(_: libc::c_double) -> libc::c_int;
}
#[c2rust::src_loc = "5354:1"]
pub type int32_t = __int32_t;
#[c2rust::src_loc = "4802:1"]
pub type __int32_t = libc::c_int;
#[c2rust::src_loc = "5396:1"]
pub type uint32_t = __uint32_t;
#[c2rust::src_loc = "4803:1"]
pub type __uint32_t = libc::c_uint;
#[derive(Copy, Clone)]
#[repr(C)]
#[c2rust::src_loc = "7046:9"]
pub union ieee_float_shape_type {
    pub value: libc::c_float,
    pub word: uint32_t,
}
#[c2rust::src_loc = "71:27"]
static mut __volatile_onef: libc::c_float = 1.0f32;
#[inline]
#[c2rust::src_loc = "7079:1"]
unsafe extern "C" fn __forced_calculationf(mut x: libc::c_float) -> libc::c_float {
    let mut r: libc::c_float = x;
    return r;
}
#[inline]
#[c2rust::src_loc = "7111:1"]
unsafe extern "C" fn __raise_overflowf(mut x: libc::c_float) -> libc::c_float {
    let mut huge: libc::c_float = 1.0e30f32;
    return if (if *(b"{\"argNames\":[\"__x\"],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7113:13\",\"cuLnColEnd\":\"7113:23\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:13\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:23\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/math.h:328:9\",\"name\":\"signbit\",\"seedType\":\"invocation\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        (if ::core::mem::size_of::<libc::c_float>() as libc::c_ulong
            == ::core::mem::size_of::<libc::c_float>() as libc::c_ulong
        {
            __signbitf(
                *(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7113:21\",\"cuLnColEnd\":\"7113:22\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:21\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:22\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:13\",\"name\":\"__x\",\"seedType\":\"invocation\"}\0"
                    as *const u8 as *const libc::c_char) as libc::c_int != 0
                {
                    &mut x
                } else {
                    0 as *mut libc::c_float
                }),
            )
        } else {
            __signbitd(
                *(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7113:21\",\"cuLnColEnd\":\"7113:22\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:21\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:22\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:13\",\"name\":\"__x\",\"seedType\":\"invocation\"}\0"
                    as *const u8 as *const libc::c_char) as libc::c_int != 0
                {
                    &mut x
                } else {
                    0 as *mut libc::c_float
                }) as libc::c_double,
            )
        })
    } else {
        *(0 as *mut libc::c_int)
    }) != 0 as libc::c_int
    {
        -__forced_calculationf(huge * huge)
    } else {
        __forced_calculationf(huge * huge)
    };
}
#[inline]
#[c2rust::src_loc = "7119:1"]
unsafe extern "C" fn __raise_inexactf(mut x: libc::c_float) -> libc::c_float {
    let mut huge: libc::c_float = 1.0e30f32;
    return if __forced_calculationf(huge - 1.0e-30f32) != 0.0f32 { x } else { 0.0f32 };
}
#[c2rust::src_loc = "7180:20"]
static mut one: libc::c_float = 1.0f32;
#[no_mangle]
#[c2rust::src_loc = "7182:1"]
pub unsafe extern "C" fn sinhf(mut x: libc::c_float) -> libc::c_float {
    *(b"{\"astKind\":\"Stmt\",\"begin\":true,\"cuLnColBegin\":\"7185:5\",\"cuLnColEnd\":\"7185:26\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:13:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:13:26\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:12:1\",\"premise\":\"defLIBMCS_FPU_DAZ\",\"seedType\":\"conditional\"}\0"
        as *const u8 as *const libc::c_char);
    x *= __volatile_onef;
    *(b"{\"astKind\":\"Stmt\",\"begin\":false,\"cuLnColBegin\":\"7185:5\",\"cuLnColEnd\":\"7185:26\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:13:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:13:26\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:12:1\",\"premise\":\"defLIBMCS_FPU_DAZ\",\"seedType\":\"conditional\"}\0"
        as *const u8 as *const libc::c_char);
    let mut t: libc::c_float = 0.;
    let mut w: libc::c_float = 0.;
    let mut h: libc::c_float = 0.;
    let mut ix: int32_t = 0;
    let mut jx: int32_t = 0;
    *(b"{\"argNames\":[\"i\",\"d\"],\"astKind\":\"Stmt\",\"begin\":true,\"canBeFn\":true,\"cuLnColBegin\":\"7192:5\",\"cuLnColEnd\":\"7192:26\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:26\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:307:9\",\"name\":\"GET_FLOAT_WORD\",\"seedType\":\"invocation\"}\0"
        as *const u8 as *const libc::c_char);
    loop {
        let mut gf_u: ieee_float_shape_type = ieee_float_shape_type { value: 0. };
        gf_u
            .value = *if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7192:24\",\"cuLnColEnd\":\"7192:25\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:24\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:25\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"name\":\"d\",\"seedType\":\"invocation\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            &mut x
        } else {
            0 as *mut libc::c_float
        };
        *if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7192:20\",\"cuLnColEnd\":\"7192:22\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:20\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:22\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"name\":\"i\",\"seedType\":\"invocation\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            &mut jx
        } else {
            0 as *mut int32_t
        } = gf_u.word as int32_t;
        if !(0 as libc::c_int == 1 as libc::c_int) {
            break;
        }
    }
    *(b"{\"argNames\":[\"i\",\"d\"],\"astKind\":\"Stmt\",\"begin\":false,\"canBeFn\":true,\"cuLnColBegin\":\"7192:5\",\"cuLnColEnd\":\"7192:26\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:26\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:307:9\",\"name\":\"GET_FLOAT_WORD\",\"seedType\":\"invocation\"}\0"
        as *const u8 as *const libc::c_char);
    ix = jx & 0x7fffffff as libc::c_int;
    if if *(b"{\"argNames\":[\"x\"],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"cuLnColBegin\":\"7196:10\",\"cuLnColEnd\":\"7196:33\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:10\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:33\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:90:9\",\"name\":\"FLT_UWORD_IS_FINITE\",\"seedType\":\"invocation\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        ((*(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7196:30\",\"cuLnColEnd\":\"7196:32\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:30\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:32\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:10\",\"name\":\"x\",\"seedType\":\"invocation\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            &mut ix as *mut int32_t
        } else {
            0 as *mut int32_t
        }) as libc::c_long) < 0x7f800000 as libc::c_long) as libc::c_int
    } else {
        *(0 as *mut libc::c_int)
    } == 0
    {
        return x + x;
    }
    h = 0.5f32;
    if jx < 0 as libc::c_int {
        h = -h;
    }
    if ix < 0x41b00000 as libc::c_int {
        if ix < 0x31800000 as libc::c_int {
            if if *(b"{\"argNames\":[\"x\"],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"cuLnColBegin\":\"7209:17\",\"cuLnColEnd\":\"7209:38\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:17\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:38\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:129:13\",\"name\":\"FLT_UWORD_IS_ZERO\",\"seedType\":\"invocation\"}\0"
                as *const u8 as *const libc::c_char) as libc::c_int != 0
            {
                ((*(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"cuLnColBegin\":\"7209:35\",\"cuLnColEnd\":\"7209:37\",\"hayroll\":true,\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:35\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:37\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:17\",\"name\":\"x\",\"seedType\":\"invocation\"}\0"
                    as *const u8 as *const libc::c_char) as libc::c_int != 0
                {
                    &mut ix as *mut int32_t
                } else {
                    0 as *mut int32_t
                }) as libc::c_long) < 0x800000 as libc::c_long) as libc::c_int
            } else {
                *(0 as *mut libc::c_int)
            } != 0
            {
                return x
            } else {
                return __raise_inexactf(x)
            }
        }
        t = expm1f(fabsf(x));
        if ix < 0x3f800000 as libc::c_int {
            return h * (2.0f32 * t - t * t / (t + one));
        }
        return h * (t + t / (t + one));
    }
    if ix
        <= (if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"cuLnColBegin\":\"7226:15\",\"cuLnColEnd\":\"7226:32\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:53:15\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:53:32\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:95:9\",\"name\":\"FLT_UWORD_LOG_MAX\",\"seedType\":\"invocation\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            0x42b17217 as libc::c_int
        } else {
            *(0 as *mut libc::c_int)
        })
    {
        return h * expf(fabsf(x));
    }
    if ix
        <= (if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"cuLnColBegin\":\"7231:15\",\"cuLnColEnd\":\"7231:33\",\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:58:15\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:58:33\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:96:9\",\"name\":\"FLT_UWORD_LOG_2MAX\",\"seedType\":\"invocation\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            0x42b2d4fc as libc::c_int
        } else {
            *(0 as *mut libc::c_int)
        })
    {
        w = expf(0.5f32 * fabsf(x));
        t = h * w;
        return t * w;
    }
    return __raise_overflowf(x);
}
#[no_mangle]
#[c2rust::src_loc = "7250:15"]
pub static mut HAYROLL_TAG_FOR_70_5_71_50_0b66cda3: *const libc::c_char = b"{\"astKind\":\"Decls\",\"begin\":true,\"cuLnColBegin\":\"70:5\",\"cuLnColEnd\":\"71:50\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:19:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:20:50\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:17:1\",\"premise\":\"defLIBMCS_FPU_DAZ\",\"seedType\":\"conditional\"}\0"
    as *const u8 as *const libc::c_char;
#[no_mangle]
#[c2rust::src_loc = "7251:15"]
pub static mut HAYROLL_TAG_FOR_444_1_528_7_1aaaaa93: *const libc::c_char = b"{\"astKind\":\"Decls\",\"begin\":true,\"cuLnColBegin\":\"446:5\",\"cuLnColEnd\":\"526:67\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/include/math.h:223:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/include/math.h:303:67\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/math.h:221:1\",\"premise\":\"defLIBMCS_LONG_DOUBLE_IS_64BITS\",\"seedType\":\"conditional\"}\0"
    as *const u8 as *const libc::c_char;
#[no_mangle]
#[c2rust::src_loc = "7252:15"]
pub static mut HAYROLL_TAG_FOR_7145_1_7173_7_341290de: *const libc::c_char = b"{\"astKind\":\"Decls\",\"begin\":true,\"cuLnColBegin\":\"7146:5\",\"cuLnColEnd\":\"7168:27\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:400:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/common/tools.h:422:27\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:399:1\",\"premise\":\"defLIBMCS_WANT_COMPLEX\",\"seedType\":\"conditional\"}\0"
    as *const u8 as *const libc::c_char;
#[no_mangle]
#[c2rust::src_loc = "7253:15"]
pub static mut HAYROLL_TAG_FOR_7241_1_7248_7_0609d535: *const libc::c_char = b"{\"astKind\":\"Decl\",\"begin\":true,\"cuLnColBegin\":\"7243:1\",\"cuLnColEnd\":\"7246:2\",\"hayroll\":true,\"isLvalue\":false,\"isPlaceholder\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:70:1\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:73:2\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:68:1\",\"premise\":\"defLIBMCS_DOUBLE_IS_32BITS\",\"seedType\":\"conditional\"}\0"
    as *const u8 as *const libc::c_char;

)";

    std::string rustStr = ReaperWrapper::runReaper(seededRustStr);
    if (rustStr == seededRustStr)
    {
        std::cerr << "Reaper did not modify the input Rust code." << std::endl;
        return 1;
    }
    std::cout << "Reaper output:\n" << rustStr << std::endl;

    return 0;
}
