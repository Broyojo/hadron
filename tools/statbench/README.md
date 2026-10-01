# statbench

Measures the cost of pipeline statistics queries on a Vulkan driver: GPU time (timestamps) and command recording time
for overdraw, overdraw with a discarding shader, and 5,000 direct and indirect draws, each with no query, every statistic,
and every statistic but fragment invocations.

    glslangValidator -V vs.vert -o vs.spv && glslangValidator -V fs.frag -o fs.spv
    clang -O2 -I/opt/homebrew/include statbench.c -L/opt/homebrew/lib -lvulkan -o statbench
    VK_DRIVER_FILES=<icd.json> ./statbench .

`NO_QUERIES=1` runs only the no-query rows (for comparing against a driver without the feature).
