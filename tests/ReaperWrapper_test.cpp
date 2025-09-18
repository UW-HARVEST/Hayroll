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
#[no_mangle]
#[c2rust::src_loc = "7332:1"]
pub unsafe extern "C" fn sinhf(mut x: libc::c_float) -> libc::c_float {
    x *= __volatile_onef;
    let mut t: libc::c_float = 0.;
    let mut w: libc::c_float = 0.;
    let mut h: libc::c_float = 0.;
    let mut ix: int32_t = 0;
    let mut jx: int32_t = 0;
    *(b"{\"argNames\":[\"i\",\"d\"],\"astKind\":\"Stmt\",\"begin\":true,\"canBeFn\":true,\"hayroll\":\"invocation\",\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:26\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:307:9\",\"name\":\"GET_FLOAT_WORD\"}\0"
        as *const u8 as *const libc::c_char);
    loop {
        let mut gf_u: ieee_float_shape_type = ieee_float_shape_type { value: 0. };
        gf_u
            .value = *if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"hayroll\":\"invocation\",\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:24\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:25\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"name\":\"d\"}\0"
            as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            &mut x
        } else {
            0 as *mut libc::c_float
        };
        *if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"hayroll\":\"invocation\",\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:20\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:22\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"name\":\"i\"}\0"
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
    *(b"{\"argNames\":[\"i\",\"d\"],\"astKind\":\"Stmt\",\"begin\":false,\"canBeFn\":true,\"hayroll\":\"invocation\",\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:5\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:19:26\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/common/tools.h:307:9\",\"name\":\"GET_FLOAT_WORD\"}\0"
        as *const u8 as *const libc::c_char);
    ix = jx & 0x7fffffff as libc::c_int;
    if if *(b"{\"argNames\":[\"x\"],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"hayroll\":\"invocation\",\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:10\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:33\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:90:9\",\"name\":\"FLT_UWORD_IS_FINITE\"}\0"
        as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        ((*(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"hayroll\":\"invocation\",\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:30\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:32\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:23:10\",\"name\":\"x\"}\0"
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
            if if *(b"{\"argNames\":[\"x\"],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"hayroll\":\"invocation\",\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:17\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:38\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:129:13\",\"name\":\"FLT_UWORD_IS_ZERO\"}\0"
                as *const u8 as *const libc::c_char) as libc::c_int != 0
            {
                ((*(if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":false,\"hayroll\":\"invocation\",\"isArg\":true,\"isLvalue\":true,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:35\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:37\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:36:17\",\"name\":\"x\"}\0"
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
        <= (if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"hayroll\":\"invocation\",\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:53:15\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:53:32\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:95:9\",\"name\":\"FLT_UWORD_LOG_MAX\"}\0"
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
        <= (if *(b"{\"argNames\":[],\"astKind\":\"Expr\",\"begin\":true,\"canBeFn\":true,\"hayroll\":\"invocation\",\"isArg\":false,\"isLvalue\":false,\"locBegin\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:58:15\",\"locEnd\":\"/home/hurrypeng/libmcs/libm/mathf/sinhf.c:58:33\",\"locRefBegin\":\"/home/hurrypeng/libmcs/libm/include/internal_config.h:96:9\",\"name\":\"FLT_UWORD_LOG_2MAX\"}\0"
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
