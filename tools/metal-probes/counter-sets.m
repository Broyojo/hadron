#import <Metal/Metal.h>
#include <stdio.h>
int main(void){@autoreleasepool{
 id<MTLDevice> d=MTLCreateSystemDefaultDevice();
 printf("%s\n", d.name.UTF8String);
 for (id<MTLCounterSet> s in d.counterSets){printf("set %s:", s.name.UTF8String); for(id<MTLCounter> c in s.counters) printf(" %s", c.name.UTF8String); printf("\n");}
 printf("stageboundary=%d drawboundary=%d dispatchboundary=%d blit=%d tile=%d\n",
  [d supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary],[d supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary],
  [d supportsCounterSampling:MTLCounterSamplingPointAtDispatchBoundary],[d supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary],
  [d supportsCounterSampling:MTLCounterSamplingPointAtTileDispatchBoundary]);
 for (int t=0;t<4;t++){ MTL4CounterHeapDescriptor *hd=[MTL4CounterHeapDescriptor new]; hd.type=(MTL4CounterHeapType)t; hd.count=4; NSError*e=nil; id h=[d newCounterHeapWithDescriptor:hd error:&e]; printf("heap type %d: %p %s\n",t,h,e?e.localizedDescription.UTF8String:"");}
}}
