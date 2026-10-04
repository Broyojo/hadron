#include <metal_stdlib>
using namespace metal;
struct Buffer {
    uint64_t contents[1];
};
struct SamplerTable {
    sampler handles[4096];
};
kernel void main_entrypoint(
    uint gl_SubGroupInvocation [[thread_index_in_simdgroup]],
    uint3 gl_LocalInvocationID [[thread_position_in_threadgroup]],
    uint3 gl_WorkGroupID [[threadgroup_position_in_grid]],
    constant Buffer &buf0 [[buffer(0)]],
    constant SamplerTable &sampler_table [[buffer(1)]]
)
{
    threadgroup char shared_data[15376];
    ulong t16;
    ulong t18;
    ulong t19;
    uint t20;
    bool t22;
    uint3 t23;
    int t25;
    int t26;
    int t28;
    int t29;
    int t30;
    int t32;
    int t33;
    uint3 t34;
    ulong t36;
    uint3 t37;
    uint t38;
    uint t39;
    uint t41;
    uint t42;
    int t44;
    int t45;
    int t46;
    int t47;
    int t50;
    int t51;
    ulong t52;
    ulong t53;
    ulong t54;
    ulong t55;
    uint t57;
    uint t59;
    uint t61;
    uint t63;
    ulong t65;
    ulong t66;
    ulong t67;
    ulong t68;
    uint t69;
    bool t74;
    bool t76;
    bool t77;
    bool t80;
    bool t82;
    uint t84;
    uint t85;
    ulong t86;
    uint t87;
    uint t89;
    bool t91;
    int t92;
    uint t94;
    uint t95;
    uint t96;
    uint t97;
    ulong t98;
    ulong t99;
    bool t101;
    bool t103;
    bool t104;
    bool t107;
    bool t109;
    uint t111;
    ulong t112;
    uint t113;
    ulong t114;
    uint t115;
    uint t117;
    bool t119;
    ulong t120;
    bool t122;
    ulong t123;
    bool t124;
    ulong t127;
    ulong t128;
    uint4 t129;
    int t130;
    bool t131;
    uint t133;
    int t134;
    uint t136;
    uint t138;
    uint t139;
    ulong t140;
    ulong t141;
    uchar t142;
    bool t144;
    ulong t145;
    ulong t146;
    ulong t147;
    ulong t148;
    ulong t149;
    ulong t150;
    ulong t151;
    ulong t152;
    ulong t153;
    ulong t154;
    ulong t155;
    ulong t156;
    uint3 t157;
    int t159;
    int t160;
    int t162;
    int t163;
    int t164;
    int t166;
    int t167;
    uint3 t168;
    ulong t170;
    uint3 t171;
    uint t172;
    uint t173;
    uint t175;
    uint t176;
    int t178;
    int t179;
    int t180;
    int t181;
    int t184;
    int t185;
    ulong t186;
    ulong t187;
    ulong t188;
    ulong t189;
    uint t191;
    uint t193;
    uint t195;
    uint t197;
    ulong t199;
    ulong t200;
    ulong t201;
    ulong t202;
    uint t203;
    bool t208;
    bool t210;
    bool t211;
    bool t214;
    bool t216;
    uint t218;
    uint t219;
    ulong t220;
    uint t221;
    uint t223;
    bool t225;
    int t226;
    uint t228;
    uint t229;
    uint t230;
    uint t231;
    ulong t232;
    ulong t233;
    bool t235;
    bool t237;
    bool t238;
    bool t241;
    bool t243;
    uint t245;
    ulong t246;
    uint t247;
    ulong t248;
    uint t249;
    uint t251;
    bool t253;
    ulong t254;
    bool t256;
    ulong t257;
    bool t258;
    ulong t261;
    ulong t262;
    uint4 t263;
    int t264;
    ulong t265;
    ulong t266;
    ulong t267;
    ulong t268;
    ulong t269;
    ulong t270;
    bool r0 = bool(0);
    uint r1 = uint(0);
    uint r2 = uint(0);
    bool r3 = bool(0);
    uint r4 = uint(0);
    uint r5 = uint(0);
    ulong r6 = ulong(0);
    bool r7 = bool(0);
    bool r8 = bool(0);
    uint r9 = uint(0);
    uint r10 = uint(0);
    bool r11 = bool(0);
    uint r12 = uint(0);
    uint r13 = uint(0);
    ulong r14 = ulong(0);
    bool r15 = bool(0);
    t16 = (ulong)&buf0.contents[0];
    t18 = t16 + ulong(2240u);
    t19 = *(constant ulong*)(t18);
    t20 = *(constant uint*)(t19);
    {
    #pragma METAL fp math_mode(safe)
    t22 = t20 != uint(0u);
    }
    if (t22) {
        t23 = gl_LocalInvocationID;
        t25 = as_type<int3>(t23).y * int(31);
        t26 = t25 + as_type<int3>(t23).x;
        t28 = -as_type<int3>(t23).x;
        t29 = int(30) + t28;
        t30 = -t25;
        t32 = t30 + int(930);
        t33 = t32 + t29;
        t34 = gl_WorkGroupID;
        t36 = t16 + ulong(8u);
        t37 = uint3(*(constant packed_uint3*)t36);
        t38 = t34.y + t37.y;
        t39 = t34.x + t37.x;
        t41 = t38 << uint(3u);
        t42 = t41 + t39;
        t44 = int(961) * as_type<int>(t42);
        t45 = t44 + t26;
        t46 = t44 + t33;
        t47 = t26 << int(3);
        (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t47)]) = ulong(0u);
        t50 = int(7688) + t47;
        (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t50)]) = ulong(0u);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        t51 = t33 << int(3);
        t52 = *(threadgroup ulong*)&shared_data[0 + as_type<uint>(t51)];
        t53 = t52 >> uint(31u);
        t54 = ulong(as_type<uint>(t45));
        t55 = t54 + t53;
        (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t47)]) = t55;
        atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
        t57 = as_type<uint>(t50) >> uint(3u);
        t59 = t57 & uint(4095u);
        t61 = t59 << uint(2u);
        t63 = t61 + uint(64u);
        t65 = t16 + ulong(2248u);
        t66 = *(constant ulong*)(t65);
        t67 = ulong(t63);
        t68 = t66 + t67;
        t69 = gl_SubGroupInvocation;
        r2 = as_type<uint>(uint(uint(0u)));
        while (true) {
            {
            #pragma METAL fp math_mode(safe)
            t74 = as_type<uint>(r2) == t69;
            }
            if (t74) {
                r1 = as_type<uint>(uint(uint(0u)));
                while (true) {
                    uint ta75 = uint(0u); atomic_compare_exchange_weak_explicit((device atomic_uint*)t68, &ta75, uint(1u), memory_order_relaxed, memory_order_relaxed);uint t75 = ta75;
                    {
                    #pragma METAL fp math_mode(safe)
                    t76 = t75 == uint(0u);
                    }
                    {
                    #pragma METAL fp math_mode(safe)
                    t77 = !t76;
                    }
                    if (t77) {
                        {
                        #pragma METAL fp math_mode(safe)
                        t80 = as_type<uint>(r1) >= uint(4194304u);
                        }
                        r0 = bool((t80));
                    }
                    t82 = bool(r0) | t76;
                    if (t82) {
                        break;
                    }
                    t84 = as_type<uint>(r1) + uint(1u);
                    r1 = as_type<uint>((t84));
                }
                if (t76) {
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t50)]) = ulong(1u);
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    t85 = atomic_exchange_explicit((device atomic_uint*)t68, uint(0u), memory_order_relaxed);
                } else {
                    t86 = t66 + ulong(8u);
                    t87 = atomic_exchange_explicit((device atomic_uint*)t86, uint(1u), memory_order_relaxed);
                }
            }
            t89 = as_type<uint>(r2) + uint(1u);
            r2 = as_type<uint>((t89));
            {
            #pragma METAL fp math_mode(safe)
            t91 = as_type<uint>(r2) >= uint(32u);
            }
            if (t91) {
                break;
            }
        }
        t92 = int(7688) + t51;
        t94 = as_type<uint>(t92) >> uint(3u);
        t95 = t94 & uint(4095u);
        t96 = t95 << uint(2u);
        t97 = t96 + uint(64u);
        t98 = ulong(t97);
        t99 = t66 + t98;
        r5 = as_type<uint>(uint(uint(0u)));
        r6 = as_type<ulong>(ulong(ulong(0u)));
        while (true) {
            {
            #pragma METAL fp math_mode(safe)
            t101 = as_type<uint>(r5) == t69;
            }
            if (t101) {
                r4 = as_type<uint>(uint(uint(0u)));
                while (true) {
                    uint ta102 = uint(0u); atomic_compare_exchange_weak_explicit((device atomic_uint*)t99, &ta102, uint(1u), memory_order_relaxed, memory_order_relaxed);uint t102 = ta102;
                    {
                    #pragma METAL fp math_mode(safe)
                    t103 = t102 == uint(0u);
                    }
                    {
                    #pragma METAL fp math_mode(safe)
                    t104 = !t103;
                    }
                    if (t104) {
                        {
                        #pragma METAL fp math_mode(safe)
                        t107 = as_type<uint>(r4) >= uint(4194304u);
                        }
                        r3 = bool((t107));
                    }
                    t109 = bool(r3) | t103;
                    if (t109) {
                        break;
                    }
                    t111 = as_type<uint>(r4) + uint(1u);
                    r4 = as_type<uint>((t111));
                }
                if (t103) {
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    t112 = *(threadgroup ulong*)&shared_data[0 + as_type<uint>(t92)];
                    r6 = as_type<ulong>((t112));
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t92)]) = ulong(2u);
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    t113 = atomic_exchange_explicit((device atomic_uint*)t99, uint(0u), memory_order_relaxed);
                } else {
                    t114 = t66 + ulong(8u);
                    t115 = atomic_exchange_explicit((device atomic_uint*)t114, uint(1u), memory_order_relaxed);
                }
            }
            t117 = as_type<uint>(r5) + uint(1u);
            r5 = as_type<uint>((t117));
            {
            #pragma METAL fp math_mode(safe)
            t119 = as_type<uint>(r5) >= uint(32u);
            }
            if (t119) {
                break;
            }
        }
        atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
        t120 = *(threadgroup ulong*)&shared_data[0 + as_type<uint>(t51)];
        {
        #pragma METAL fp math_mode(safe)
        t122 = as_type<ulong>(r6) != ulong(0u);
        }
        if (t122) {
            t123 = ulong(as_type<uint>(t46));
            {
            #pragma METAL fp math_mode(safe)
            t124 = t120 != t123;
            }
            r7 = bool((t124));
        } else {
            r7 = bool((bool(0)));
        }
        if (bool(r7)) {
            t127 = t16 + ulong(928u);
            t128 = *(constant ulong*)(t127);
            t129 = uint4(*(device packed_uint4*)(t128 + uint(32u)));
            t130 = t45 << int(2);
            {
            #pragma METAL fp math_mode(safe)
            t131 = t129.w != uint(0u);
            }
            if (t131) {
                t133 = t129.x & uint(65535u);
                t134 = as_type<int>(t133) + t130;
                t136 = as_type<uint>(t134) >> uint(16u);
                t138 = t129.w + uint(4294967295u);
                t139 = t138 + t136;
                t140 = ulong(t139);
                t141 = t19 + t140;
                t142 = *(device uchar*)(t141);
                {
                #pragma METAL fp math_mode(safe)
                t144 = t142 != uchar(0u);
                }
                if (t144) {
                    t145 = ulong(t129.x);
                    t146 = ulong(t129.y);
                    t147 = t146 << uint(32u);
                    t148 = t145 | t147;
                    t149 = ulong(as_type<uint>(t130));
                    t150 = t148 + t149;
                    (*(device uint*)t150) = uint(uint(1u));
                }
            } else {
                t151 = ulong(t129.x);
                t152 = ulong(t129.y);
                t153 = t152 << uint(32u);
                t154 = t151 | t153;
                t155 = ulong(as_type<uint>(t130));
                t156 = t154 + t155;
                (*(device uint*)t156) = uint(uint(1u));
            }
        }
    } else {
        t157 = gl_LocalInvocationID;
        t159 = as_type<int3>(t157).y * int(31);
        t160 = t159 + as_type<int3>(t157).x;
        t162 = -as_type<int3>(t157).x;
        t163 = int(30) + t162;
        t164 = -t159;
        t166 = t164 + int(930);
        t167 = t166 + t163;
        t168 = gl_WorkGroupID;
        t170 = t16 + ulong(8u);
        t171 = uint3(*(constant packed_uint3*)t170);
        t172 = t168.y + t171.y;
        t173 = t168.x + t171.x;
        t175 = t172 << uint(3u);
        t176 = t175 + t173;
        t178 = int(961) * as_type<int>(t176);
        t179 = t178 + t160;
        t180 = t178 + t167;
        t181 = t160 << int(3);
        (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t181)]) = ulong(0u);
        t184 = int(7688) + t181;
        (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t184)]) = ulong(0u);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        t185 = t167 << int(3);
        t186 = *(threadgroup ulong*)&shared_data[0 + as_type<uint>(t185)];
        t187 = t186 >> uint(31u);
        t188 = ulong(as_type<uint>(t179));
        t189 = t188 + t187;
        (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t181)]) = t189;
        atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
        t191 = as_type<uint>(t184) >> uint(3u);
        t193 = t191 & uint(4095u);
        t195 = t193 << uint(2u);
        t197 = t195 + uint(64u);
        t199 = t16 + ulong(2248u);
        t200 = *(constant ulong*)(t199);
        t201 = ulong(t197);
        t202 = t200 + t201;
        t203 = gl_SubGroupInvocation;
        r10 = as_type<uint>(uint(uint(0u)));
        while (true) {
            {
            #pragma METAL fp math_mode(safe)
            t208 = as_type<uint>(r10) == t203;
            }
            if (t208) {
                r9 = as_type<uint>(uint(uint(0u)));
                while (true) {
                    uint ta209 = uint(0u); atomic_compare_exchange_weak_explicit((device atomic_uint*)t202, &ta209, uint(1u), memory_order_relaxed, memory_order_relaxed);uint t209 = ta209;
                    {
                    #pragma METAL fp math_mode(safe)
                    t210 = t209 == uint(0u);
                    }
                    {
                    #pragma METAL fp math_mode(safe)
                    t211 = !t210;
                    }
                    if (t211) {
                        {
                        #pragma METAL fp math_mode(safe)
                        t214 = as_type<uint>(r9) >= uint(4194304u);
                        }
                        r8 = bool((t214));
                    }
                    t216 = bool(r8) | t210;
                    if (t216) {
                        break;
                    }
                    t218 = as_type<uint>(r9) + uint(1u);
                    r9 = as_type<uint>((t218));
                }
                if (t210) {
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t184)]) = ulong(1u);
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    t219 = atomic_exchange_explicit((device atomic_uint*)t202, uint(0u), memory_order_relaxed);
                } else {
                    t220 = t200 + ulong(8u);
                    t221 = atomic_exchange_explicit((device atomic_uint*)t220, uint(1u), memory_order_relaxed);
                }
            }
            t223 = as_type<uint>(r10) + uint(1u);
            r10 = as_type<uint>((t223));
            {
            #pragma METAL fp math_mode(safe)
            t225 = as_type<uint>(r10) >= uint(32u);
            }
            if (t225) {
                break;
            }
        }
        t226 = int(7688) + t185;
        t228 = as_type<uint>(t226) >> uint(3u);
        t229 = t228 & uint(4095u);
        t230 = t229 << uint(2u);
        t231 = t230 + uint(64u);
        t232 = ulong(t231);
        t233 = t200 + t232;
        r13 = as_type<uint>(uint(uint(0u)));
        r14 = as_type<ulong>(ulong(ulong(0u)));
        while (true) {
            {
            #pragma METAL fp math_mode(safe)
            t235 = as_type<uint>(r13) == t203;
            }
            if (t235) {
                r12 = as_type<uint>(uint(uint(0u)));
                while (true) {
                    uint ta236 = uint(0u); atomic_compare_exchange_weak_explicit((device atomic_uint*)t233, &ta236, uint(1u), memory_order_relaxed, memory_order_relaxed);uint t236 = ta236;
                    {
                    #pragma METAL fp math_mode(safe)
                    t237 = t236 == uint(0u);
                    }
                    {
                    #pragma METAL fp math_mode(safe)
                    t238 = !t237;
                    }
                    if (t238) {
                        {
                        #pragma METAL fp math_mode(safe)
                        t241 = as_type<uint>(r12) >= uint(4194304u);
                        }
                        r11 = bool((t241));
                    }
                    t243 = bool(r11) | t237;
                    if (t243) {
                        break;
                    }
                    t245 = as_type<uint>(r12) + uint(1u);
                    r12 = as_type<uint>((t245));
                }
                if (t237) {
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    t246 = *(threadgroup ulong*)&shared_data[0 + as_type<uint>(t226)];
                    r14 = as_type<ulong>((t246));
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    (*(threadgroup ulong*)&shared_data[0 + as_type<uint>(t226)]) = ulong(2u);
                    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
                    t247 = atomic_exchange_explicit((device atomic_uint*)t233, uint(0u), memory_order_relaxed);
                } else {
                    t248 = t200 + ulong(8u);
                    t249 = atomic_exchange_explicit((device atomic_uint*)t248, uint(1u), memory_order_relaxed);
                }
            }
            t251 = as_type<uint>(r13) + uint(1u);
            r13 = as_type<uint>((t251));
            {
            #pragma METAL fp math_mode(safe)
            t253 = as_type<uint>(r13) >= uint(32u);
            }
            if (t253) {
                break;
            }
        }
        atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope::thread_scope_device);
        t254 = *(threadgroup ulong*)&shared_data[0 + as_type<uint>(t185)];
        {
        #pragma METAL fp math_mode(safe)
        t256 = as_type<ulong>(r14) != ulong(0u);
        }
        if (t256) {
            t257 = ulong(as_type<uint>(t180));
            {
            #pragma METAL fp math_mode(safe)
            t258 = t254 != t257;
            }
            r15 = bool((t258));
        } else {
            r15 = bool((bool(0)));
        }
        if (bool(r15)) {
            t261 = t16 + ulong(928u);
            t262 = *(constant ulong*)(t261);
            t263 = uint4(*(device packed_uint4*)(t262 + uint(32u)));
            t264 = t179 << int(2);
            t265 = ulong(t263.x);
            t266 = ulong(t263.y);
            t267 = t266 << uint(32u);
            t268 = t265 | t267;
            t269 = ulong(as_type<uint>(t264));
            t270 = t268 + t269;
            (*(device uint*)t270) = uint(uint(1u));
        }
    }
}
