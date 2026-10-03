/* denorm-test: 32-bit float operations in a shader that asks for denormals to be preserved,
 * checked bit for bit against the CPU. Apple GPUs flush 32-bit denormals to zero, so KosmicKrisp
 * emulates the mode (kk_nir_lower_denorm.c); the inputs lean on denormals, the smallest normals
 * and results that land between them.
 *
 *   tools/denorm-test/run.sh  builds the shader and this program, then runs it on KosmicKrisp
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "denorm-test-cs.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)
#define N 65536
#define OPS 24

static VkDevice dev;
static VkPhysicalDevice pdev;

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    exit(1);
}

static void *make_buffer(VkBuffer *buf, VkDeviceSize size)
{
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    CHECK(vkCreateBuffer(dev, &bci, NULL, buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, *buf, &req);
    VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size,
            memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory mem;
    CHECK(vkAllocateMemory(dev, &alloc, NULL, &mem));
    CHECK(vkBindBufferMemory(dev, *buf, mem, 0));
    void *map;
    CHECK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &map));
    return map;
}

static uint32_t rng_state = 0x12345678;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

static uint32_t bits_of(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float float_of(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static float pick(void)
{
    static const uint32_t special[] = {
        0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x00800000, 0x80800000,
        0x00800001, 0x00400000, 0x00000002, 0x3f800000, 0xbf800000, 0x40000000, 0x3f000000, 0x7f7fffff,
        0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0x00ffffff, 0x01000000, 0x3fc00000, 0x34000000,
    };
    uint32_t r = rng(), sign = rng() & 0x80000000;
    switch (r % 8) {
    case 0: return float_of(special[rng() % (sizeof(special) / sizeof(special[0]))]);
    case 1: case 2: return float_of(sign | (rng() & 0x007fffff));                       /* denormal */
    case 3: return float_of(sign | ((1 + rng() % 30) << 23) | (rng() & 0x007fffff));     /* just above */
    case 4: return float_of(sign | ((100 + rng() % 56) << 23) | (rng() & 0x007fffff));   /* around 1 */
    case 5: return float_of(sign | ((127 + rng() % 4) << 23) | ((rng() & 7) << 20));     /* few bits */
    case 6: return float_of(sign | ((1 + rng() % 250) << 23) | (rng() & 0x007fffff));    /* any normal */
    default: return float_of(sign | (rng() & 0x007fffff & -(1u << (rng() % 23))));       /* sparse denormal */
    }
}

static int is_nan(float f) { return (bits_of(f) & 0x7fffffff) > 0x7f800000; }

/* 0 exact, n ULPs apart otherwise (huge when signs or classes differ) */
static uint32_t ulps(float got, float want)
{
    if (is_nan(got) && is_nan(want))
        return 0;
    uint32_t g = bits_of(got), w = bits_of(want);
    if (g == w)
        return 0;
    if ((g ^ w) & 0x80000000)
        return ((g | w) & 0x7fffffff) == 0 ? 0xffffffff : (g & 0x7fffffff) + (w & 0x7fffffff);
    return g > w ? g - w : w - g;
}

int main(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "denorm-test", 1, NULL, 0, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    CHECK(vkEnumeratePhysicalDevices(inst, &n, &pdev) < 0);

    VkPhysicalDeviceVulkan12Properties p12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p12 };
    vkGetPhysicalDeviceProperties2(pdev, &p2);
    printf("%s: shaderDenormPreserveFloat32 %d, shaderDenormFlushToZeroFloat32 %d\n", p2.properties.deviceName,
           p12.shaderDenormPreserveFloat32, p12.shaderDenormFlushToZeroFloat32);
    if (!p12.shaderDenormPreserveFloat32) {
        printf("the driver does not report shaderDenormPreserveFloat32\n");
        return 1;
    }

    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci };
    CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkBuffer bufs[4];
    float *a = make_buffer(&bufs[0], N * 4), *b = make_buffer(&bufs[1], N * 4), *c = make_buffer(&bufs[2], N * 4);
    float *r = make_buffer(&bufs[3], (VkDeviceSize)N * OPS * 4);
    for (int i = 0; i < N; i++) {
        a[i] = pick(); b[i] = pick(); c[i] = pick();
        if (i % 4 == 1)       /* differences and sums of near-equal tiny values */
            b[i] = float_of(bits_of(a[i]) + (rng() % 64) - 32);
        if (i % 4 == 2)       /* an exponent for ldexp and exp2 */
            c[i] = (float)((int)(rng() % 400) - 200) + (rng() % 4) * 0.25f;
    }
    memset(r, 0xee, (size_t)N * OPS * 4);

    VkDescriptorSetLayoutBinding bindings[4];
    for (int i = 0; i < 4; i++)
        bindings[i] = (VkDescriptorSetLayoutBinding){ i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 4, bindings };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
    VkDescriptorPoolSize sizes = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &sizes };
    VkDescriptorPool dpool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 1, &dsl };
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorBufferInfo infos[4];
    VkWriteDescriptorSet writes[4];
    for (int i = 0; i < 4; i++) {
        infos[i] = (VkDescriptorBufferInfo){ bufs[i], 0, VK_WHOLE_SIZE };
        writes[i] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, i, 0, 1,
                                            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &infos[i] };
    }
    vkUpdateDescriptorSets(dev, 4, writes, 0, NULL);

    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl };
    VkPipelineLayout pl;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof(denorm_test_cs), denorm_test_cs };
    VkShaderModule module;
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &module));
    VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main" }, pl };
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));

    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdDispatch(cmd, N / 64, 1, 1);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd };
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

    /* allowed error in ULPs: 0 where Vulkan asks for a correctly rounded or exact result */
    static const struct { const char *name; uint32_t tolerance; } ops[OPS] = {
        { "x + y", 0 }, { "x - y", 0 }, { "x * y", 0 }, { "x / y", 0 }, { "fma(x, y, z)", 0 },
        { "min(x, y)", 0 }, { "max(x, y)", 0 }, { "abs(x)", 0 }, { "-x", 0 },
        { "x < y", 0 }, { "x <= y", 0 }, { "x == y", 0 }, { "x != y", 0 },
        { "floor(x)", 0 }, { "ceil(x)", 0 }, { "trunc(x)", 0 }, { "sign(x)", 0 },
        { "sqrt(|x|)", 1 }, { "inversesqrt(|x|)", 2 }, { "clamp(x, lo, hi)", 0 }, { "ldexp(x, int(z))", 0 },
        { "log2(|x|)", 3 }, { "exp2(z)", 8 }, { "x * y + z", 0 },
    };
    int failures = 0;
    for (int op = 0; op < OPS; op++) {
        unsigned bad = 0, denormal_results = 0, worst = 0, checked = 0;
        int first = -1;
        for (int i = 0; i < N; i++) {
            volatile float x = a[i], y = b[i], z = c[i];
            float want, alt;
            int skip = 0;
            switch (op) {
            case 0: want = x + y; break;
            case 1: want = x - y; break;
            case 2: want = x * y; break;
            case 3: want = x / y; break;
            case 4: want = fmaf(x, y, z); { volatile float p = x * y; alt = p + z; } break;
            case 5: want = fminf(x, y); skip = is_nan(x) || is_nan(y) || (x == 0 && y == 0); break;
            case 6: want = fmaxf(x, y); skip = is_nan(x) || is_nan(y) || (x == 0 && y == 0); break;
            case 7: want = fabsf(x); break;
            case 8: want = -x; break;
            case 9: want = x < y; break;
            case 10: want = x <= y; break;
            case 11: want = x == y; break;
            case 12: want = x != y; break;
            case 13: want = floorf(x); break;
            case 14: want = ceilf(x); break;
            case 15: want = truncf(x); break;
            case 16: want = is_nan(x) ? 0 : x > 0 ? 1 : x < 0 ? -1 : x; skip = is_nan(x); break;
            case 17: want = sqrtf(fabsf(x)); break;
            case 18: want = 1.0f / sqrtf(fabsf(x)); skip = x == 0 || isinf(x); break;
            case 19: { float lo = fminf(y, z), hi = fmaxf(y, z); want = fminf(fmaxf(x, lo), hi);
                       skip = is_nan(x) || is_nan(y) || is_nan(z) || x == lo || x == hi || lo == hi; } break;
            case 20: /* GLSL leaves ldexp undefined when the result is too large for the type */
                skip = is_nan(z) || fabsf(z) > 1e6f; want = skip ? 0 : ldexpf(x, (int)z);
                skip |= isinf(want) && !isinf(x); break;
            case 21: want = log2f(fabsf(x)); skip = x == 0; break;
            case 22: want = exp2f(z); skip = is_nan(z); break;
            default: { volatile float p = x * y; want = p + z; alt = fmaf(x, y, z); } break;
            }
            if (skip)
                continue;
            checked++;
            float got = r[(size_t)op * N + i];
            uint32_t d = ulps(got, want);
            if ((op == 4 || op == 23) && ulps(got, alt) < d)
                d = ulps(got, alt);
            uint32_t w = bits_of(want) & 0x7fffffff;
            denormal_results += w != 0 && w < 0x00800000;
            if (d > ops[op].tolerance) {
                if (first < 0)
                    first = i;
                if (getenv("VERBOSE") && bad < 12)
                    printf("   x=%08x y=%08x z=%08x (%g) got %08x want %08x\n", bits_of(x), bits_of(y), bits_of(z), z,
                           bits_of(got), bits_of(want));
                bad++;
            }
            if (d > worst)
                worst = d;
        }
        printf("%-18s %6u checked, %5u denormal results, %5u wrong, worst %u ULP%s\n", ops[op].name, checked,
               denormal_results, bad, worst, bad ? "  FAIL" : "");
        if (bad) {
            int i = first;
            printf("   first: x=%08x y=%08x z=%08x got %08x\n", bits_of(a[i]), bits_of(b[i]), bits_of(c[i]),
                   bits_of(r[(size_t)op * N + i]));
        }
        failures += bad != 0;
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
