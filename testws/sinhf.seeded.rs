#![allow(dead_code, mutable_transmutes, non_camel_case_types, non_snake_case, non_upper_case_globals, unused_assignments, unused_mut)]
macro_rules! signbit
{
    ($__x:expr) => {
    if *(b"{\"argNames\":[\"__x\"],\"astKind\":\"Expr\",\"begin\":true,\"canFn\":false,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/include/math.h:328:9\",\"locInv\":\"/home/hurrypeng/libmcs/libm/common/tools.h:367:13\",\"name\":\"signbit\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        (if ::core::mem::size_of::<libc::c_float>() as libc::c_ulong
            == ::core::mem::size_of::<libc::c_float>() as libc::c_ulong
        {
            __signbitf(
                $__x,
            )
        } else {
            __signbitd(
                $__x as libc::c_double,
            )
        })
    } else {
        *(0 as *mut libc::c_int)
    }
    }
}
extern "C" {
    fn expf(_: libc::c_float) -> libc::c_float;
    fn expm1f(_: libc::c_float) -> libc::c_float;
    fn fabsf(_: libc::c_float) -> libc::c_float;
    fn __signbitf(_: libc::c_float) -> libc::c_int;
    fn __signbitd(_: libc::c_double) -> libc::c_int;
}
pub type int32_t = __int32_t;
pub type __int32_t = libc::c_int;
pub type uint32_t = __uint32_t;
pub type __uint32_t = libc::c_uint;
#[derive(Copy, Clone)]
#[repr(C)]
pub union ieee_float_shape_type {
    pub value: libc::c_float,
    pub word: uint32_t,
}
static mut one: libc::c_float = 1.0f32;
#[no_mangle]
pub unsafe extern "C" fn sinhf(mut x: libc::c_float) -> libc::c_float {
    x *= __volatile_onef;
    let mut t: libc::c_float = 0.;
    let mut w: libc::c_float = 0.;
    let mut h: libc::c_float = 0.;
    let mut ix: int32_t = 0;
    let mut jx: int32_t = 0;
    GET_FLOAT_WORD({
            &mut jx
        }, {
            &mut x
        });
    ix = jx & 0x7fffffff as libc::c_int;
    if FLT_UWORD_IS_FINITE(({
            &mut ix as *mut int32_t
        })) == 0
    {
        return x + x;
    }
    h = 0.5f32;
    if jx < 0 as libc::c_int {
        h = -h;
    }
    if ix < 0x41b00000 as libc::c_int {
        if ix < 0x31800000 as libc::c_int {
            if FLT_UWORD_IS_ZERO(({
                    &mut ix as *mut int32_t
                })) != 0
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
        <= (FLT_UWORD_LOG_MAX())
    {
        return h * expf(fabsf(x));
    }
    if ix
        <= (FLT_UWORD_LOG_2MAX())
    {
        w = expf(0.5f32 * fabsf(x));
        t = h * w;
        return t * w;
    }
    return __raise_overflowf(x);
}
static mut __volatile_onef: libc::c_float = 1.0f32;
#[inline]
unsafe extern "C" fn __forced_calculationf(mut x: libc::c_float) -> libc::c_float {
    let mut r: libc::c_float = x;
    return r;
}
#[inline]
unsafe extern "C" fn __raise_overflowf(mut x: libc::c_float) -> libc::c_float {
    let mut huge: libc::c_float = 1.0e30f32;
    return if (signbit!(*({
                    &mut x
                }))) != 0 as libc::c_int
    {
        -__forced_calculationf(huge * huge)
    } else {
        __forced_calculationf(huge * huge)
    };
}
#[inline]
unsafe extern "C" fn __raise_inexactf(mut x: libc::c_float) -> libc::c_float {
    let mut huge: libc::c_float = 1.0e30f32;
    return if __forced_calculationf(huge - 1.0e-30f32) != 0.0f32 { x } else { 0.0f32 };
}
unsafe fn FLT_UWORD_IS_ZERO(x: *mut int32_t) -> libc::c_int {
    FLT_UWORD_IS_ZERO()
}
unsafe fn FLT_UWORD_LOG_MAX() -> libc::c_int {
    FLT_UWORD_LOG_MAX()
}
unsafe fn FLT_UWORD_IS_FINITE(x: *mut int32_t) -> libc::c_int {
    FLT_UWORD_IS_FINITE()
}
unsafe fn FLT_UWORD_LOG_2MAX() -> libc::c_int {
    FLT_UWORD_LOG_2MAX()
}
unsafe fn GET_FLOAT_WORD(i: *mut int32_t, d: *mut libc::c_float) {
    GET_FLOAT_WORD();
}
unsafe fn FLT_UWORD_LOG_MAX() -> libc::c_int {
    if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canFn\":true,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:95:9\",\"locInv\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:53:15\",\"name\":\"FLT_UWORD_LOG_MAX\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            0x42b17217 as libc::c_int
        } else {
            *(0 as *mut libc::c_int)
        }
}
unsafe fn FLT_UWORD_IS_ZERO() -> libc::c_int {
    if *(b"{\"argNames\":[\"x\"],\"astKind\":\"Expr\",\"begin\":true,\"canFn\":true,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:129:13\",\"locInv\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:17\",\"name\":\"FLT_UWORD_IS_ZERO\"}\0"
                as *const u8 as *const libc::c_char) as libc::c_int != 0
            {
                ((*(x) as libc::c_long) < 0x800000 as libc::c_long) as libc::c_int
            } else {
                *(0 as *mut libc::c_int)
            }
}
unsafe fn FLT_UWORD_IS_FINITE() -> libc::c_int {
    if *(b"{\"argNames\":[\"x\"],\"astKind\":\"Expr\",\"begin\":true,\"canFn\":true,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:90:9\",\"locInv\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:10\",\"name\":\"FLT_UWORD_IS_FINITE\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        ((*(x) as libc::c_long) < 0x7f800000 as libc::c_long) as libc::c_int
    } else {
        *(0 as *mut libc::c_int)
    }
}
unsafe fn GET_FLOAT_WORD() {
    *(b"{\"argNames\":[\"i\",\"d\"],\"astKind\":\"Stmt\",\"begin\":true,\"canFn\":true,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/common/tools.h:307:9\",\"locInv\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"name\":\"GET_FLOAT_WORD\"}\0"
        as *const u8 as *const libc::c_char);
loop {
        let mut gf_u: ieee_float_shape_type = ieee_float_shape_type { value: 0. };
        gf_u
            .value = *d;
        *i = gf_u.word as int32_t;
        if !(0 as libc::c_int == 1 as libc::c_int) {
            break;
        }
    }
*(b"{\"argNames\":[],\"astKind\":\"\",\"begin\":false,\"canFn\":true,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/common/tools.h:307:9\",\"locInv\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"name\":\"GET_FLOAT_WORD\"}\0"
        as *const u8 as *const libc::c_char);
}
unsafe fn FLT_UWORD_LOG_2MAX() -> libc::c_int {
    if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canFn\":true,\"hayroll\":true,\"isArg\":false,\"isLvalue\":false,\"locArg\":\"\",\"locDecl\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:96:9\",\"locInv\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:58:15\",\"name\":\"FLT_UWORD_LOG_2MAX\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            0x42b2d4fc as libc::c_int
        } else {
            *(0 as *mut libc::c_int)
        }
}
