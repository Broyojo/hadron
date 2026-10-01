#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, r_); exit(1); } } while (0)

#define W 1920
#define H 1080
#define SMALL_DRAWS 5000
#define LAYERS 32
#define RUNS 15

static VkDevice dev;
static VkPhysicalDevice pd;
static VkQueue queue;
static VkCommandPool pool;
static VkPipelineLayout layout;
static float ts_period;

static uint32_t find_mem(uint32_t bits, VkMemoryPropertyFlags flags)
{
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
         return i;
   exit(2);
}

static VkShaderModule load_shader(const char *path)
{
   FILE *f = fopen(path, "rb");
   if (!f) { perror(path); exit(1); }
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   uint32_t *code = malloc(n);
   fread(code, 1, n, f);
   fclose(f);
   VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = n, .pCode = code};
   VkShaderModule m;
   CHECK(vkCreateShaderModule(dev, &ci, NULL, &m));
   free(code);
   return m;
}

static VkImageView make_image(VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
   VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
      .extent = {W, H, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
   VkImage img;
   CHECK(vkCreateImage(dev, &ci, NULL, &img));
   VkMemoryRequirements mr;
   vkGetImageMemoryRequirements(dev, img, &mr);
   VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
      .memoryTypeIndex = find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
   VkDeviceMemory mem;
   CHECK(vkAllocateMemory(dev, &ai, NULL, &mem));
   CHECK(vkBindImageMemory(dev, img, mem, 0));
   VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
      .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt, .subresourceRange = {aspect, 0, 1, 0, 1}};
   VkImageView view;
   CHECK(vkCreateImageView(dev, &vi, NULL, &view));
   return view;
}

static VkPipeline make_pipeline(VkShaderModule vs, VkShaderModule fs, int discard, int iters)
{
   int spec_data[2] = {discard, iters};
   VkSpecializationMapEntry entries[2] = {{0, 0, 4}, {1, 4, 4}};
   VkSpecializationInfo spec = {2, entries, sizeof(spec_data), spec_data};
   VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main", .pSpecializationInfo = &spec},
   };
   VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
   VkViewport vp = {0, 0, W, H, 0, 1};
   VkRect2D sc = {{0, 0}, {W, H}};
   VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc};
   VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1};
   VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = 1, .depthWriteEnable = 1, .depthCompareOp = VK_COMPARE_OP_LESS};
   VkPipelineColorBlendAttachmentState att = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &att};
   VkFormat cfmt = VK_FORMAT_R8G8B8A8_UNORM;
   VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &cfmt, .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT};
   VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri, .stageCount = 2, .pStages = stages,
      .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs,
      .pMultisampleState = &ms, .pDepthStencilState = &ds, .pColorBlendState = &cb, .layout = layout};
   VkPipeline p;
   CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &p));
   return p;
}

static double now_ms(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int cmp_d(const void *a, const void *b)
{
   double x = *(const double *)a, y = *(const double *)b;
   return x < y ? -1 : x > y;
}

enum scenario { OVERDRAW, OVERDRAW_DISCARD, SMALL_DIRECT, SMALL_INDIRECT, SCENARIO_COUNT };
static const char *scenario_names[] = {"overdraw 32x fullscreen", "overdraw + discard", "5000 small direct draws", "5000 small indirect draws"};

int main(int argc, char **argv)
{
   const char *dir = argc > 1 ? argv[1] : ".";
   int with_queries = !getenv("NO_QUERIES");

   VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
   const char *inst_ext[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
   VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
      .pApplicationInfo = &app, .enabledExtensionCount = 1, .ppEnabledExtensionNames = inst_ext};
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
   uint32_t n = 1;
   vkEnumeratePhysicalDevices(inst, &n, &pd);
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(pd, &props);
   ts_period = props.limits.timestampPeriod;
   VkPhysicalDeviceFeatures supported;
   vkGetPhysicalDeviceFeatures(pd, &supported);
   if (!supported.pipelineStatisticsQuery)
      with_queries = 0;
   printf("device: %s, pipelineStatisticsQuery=%d\n", props.deviceName, supported.pipelineStatisticsQuery);

   float prio = 1;
   VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio};
   VkPhysicalDeviceFeatures feats = {.pipelineStatisticsQuery = supported.pipelineStatisticsQuery, .multiDrawIndirect = 1};
   VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = 1};
   const char *dev_ext[] = {"VK_KHR_portability_subset"};
   VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
      .pEnabledFeatures = &feats, .enabledExtensionCount = 0, .ppEnabledExtensionNames = dev_ext};
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   vkGetDeviceQueue(dev, 0, 0, &queue);

   VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
   CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));

   VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT, 0, 4};
   VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
   CHECK(vkCreatePipelineLayout(dev, &lci, NULL, &layout));

   char path[1024];
   snprintf(path, sizeof(path), "%s/vs.spv", dir);
   VkShaderModule vs = load_shader(path);
   snprintf(path, sizeof(path), "%s/fs.spv", dir);
   VkShaderModule fs = load_shader(path);
   VkPipeline heavy = make_pipeline(vs, fs, 0, 64);
   VkPipeline heavy_discard = make_pipeline(vs, fs, 1, 64);
   VkPipeline cheap = make_pipeline(vs, fs, 0, 1);

   VkImageView color = make_image(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
   VkImageView depth = make_image(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

   VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = SMALL_DRAWS * sizeof(VkDrawIndirectCommand), .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT};
   VkBuffer ibuf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &ibuf));
   VkMemoryRequirements mr;
   vkGetBufferMemoryRequirements(dev, ibuf, &mr);
   VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
      .memoryTypeIndex = find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
   VkDeviceMemory imem;
   CHECK(vkAllocateMemory(dev, &ai, NULL, &imem));
   CHECK(vkBindBufferMemory(dev, ibuf, imem, 0));
   VkDrawIndirectCommand *cmds;
   CHECK(vkMapMemory(dev, imem, 0, VK_WHOLE_SIZE, 0, (void **)&cmds));
   for (uint32_t i = 0; i < SMALL_DRAWS; i++)
      cmds[i] = (VkDrawIndirectCommand){3, 1, 0, i};

   VkQueryPoolCreateInfo tqci = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2};
   VkQueryPool ts_pool;
   CHECK(vkCreateQueryPool(dev, &tqci, NULL, &ts_pool));
   VkQueryPool stat_pools[3] = {0};
   const VkQueryPipelineStatisticFlags all = 0x7ff;
   const VkQueryPipelineStatisticFlags modes_flags[3] = {0, all, all & ~VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT};
   const char *mode_names[3] = {"no query", "stats (all)", "stats (no FS)"};
   int mode_count = with_queries ? 3 : 1;
   for (int m = 1; m < mode_count; m++) {
      VkQueryPoolCreateInfo sci = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS,
         .queryCount = 1, .pipelineStatistics = modes_flags[m]};
      CHECK(vkCreateQueryPool(dev, &sci, NULL, &stat_pools[m]));
   }

   VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cb;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cb));
   VkFence fence;
   VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));

   printf("%-28s %-15s %12s %12s   %s\n", "scenario", "mode", "GPU ms", "record ms", "FS invocations");
   for (int s = 0; s < SCENARIO_COUNT; s++) {
      for (int m = 0; m < mode_count; m++) {
         double gpu[RUNS], cpu[RUNS];
         uint64_t fs_inv = 0;
         for (int run = 0; run < RUNS + 3; run++) {
            double t0 = now_ms();
            CHECK(vkResetCommandBuffer(cb, 0));
            VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            CHECK(vkBeginCommandBuffer(cb, &bi));
            vkCmdResetQueryPool(cb, ts_pool, 0, 2);
            if (m)
               vkCmdResetQueryPool(cb, stat_pools[m], 0, 1);

            VkImageMemoryBarrier2 barriers[2] = {
               {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
               {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL},
            };
            (void)barriers;

            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ts_pool, 0);
            if (m)
               vkCmdBeginQuery(cb, stat_pools[m], 0, 0);

            VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = color, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
            VkRenderingAttachmentInfo da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = depth, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
               .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .clearValue.depthStencil = {1.0f, 0}};
            VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {W, H}}, .layerCount = 1,
               .colorAttachmentCount = 1, .pColorAttachments = &ca, .pDepthAttachment = &da};
            vkCmdBeginRendering(cb, &ri);

            int mode = (s == SMALL_DIRECT || s == SMALL_INDIRECT) ? 1 : 0;
            vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 4, &mode);
            switch (s) {
            case OVERDRAW:
            case OVERDRAW_DISCARD:
               vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s == OVERDRAW ? heavy : heavy_discard);
               for (uint32_t i = 0; i < LAYERS; i++)
                  vkCmdDraw(cb, 3, 1, 0, i);
               break;
            case SMALL_DIRECT:
               vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cheap);
               for (uint32_t i = 0; i < SMALL_DRAWS; i++)
                  vkCmdDraw(cb, 3, 1, 0, i);
               break;
            case SMALL_INDIRECT:
               vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cheap);
               for (uint32_t i = 0; i < SMALL_DRAWS; i++)
                  vkCmdDrawIndirect(cb, ibuf, i * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
               break;
            }
            vkCmdEndRendering(cb);
            if (m)
               vkCmdEndQuery(cb, stat_pools[m], 0);
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ts_pool, 1);
            CHECK(vkEndCommandBuffer(cb));
            double t1 = now_ms();

            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
            CHECK(vkQueueSubmit(queue, 1, &si, fence));
            CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
            CHECK(vkResetFences(dev, 1, &fence));

            uint64_t ts[2];
            CHECK(vkGetQueryPoolResults(dev, ts_pool, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            if (m) {
               uint64_t stats[11] = {0};
               CHECK(vkGetQueryPoolResults(dev, stat_pools[m], 0, 1, sizeof(stats), stats, sizeof(stats), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
               fs_inv = (modes_flags[m] & VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT) ? stats[7] : 0;
            }
            if (run >= 3) {
               gpu[run - 3] = (ts[1] - ts[0]) * ts_period / 1e6;
               cpu[run - 3] = t1 - t0;
            }
         }
         qsort(gpu, RUNS, sizeof(double), cmp_d);
         qsort(cpu, RUNS, sizeof(double), cmp_d);
         printf("%-28s %-15s %12.3f %12.3f   %llu\n", scenario_names[s], mode_names[m], gpu[RUNS / 2], cpu[RUNS / 2],
                (unsigned long long)fs_inv);
      }
   }
   return 0;
}
