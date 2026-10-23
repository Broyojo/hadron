#import <AppKit/AppKit.h>
#import "scroll.h"
#include <assert.h>
#include <math.h>

int main(void)
{
    @autoreleasepool {
        unsigned int checks = 0;
        for (int continuous = 0; continuous < 2; continuous++)
        for (int shift = 0; shift < 2; shift++)
        for (int axis = 0; axis < 3; axis++)
        for (int sign = -1; sign <= 1; sign += 2)
        for (int raw = 0; raw < 2; raw++)
        {
            int vertical = axis == 1 ? 0 : sign;
            int horizontal = axis == 0 ? 0 : 2 * sign;
            CGEventRef cg = CGEventCreateScrollWheelEvent(NULL,
                continuous ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine,
                2, vertical, horizontal);
            CGEventSetIntegerValueField(cg, kCGScrollWheelEventIsContinuous, continuous);
            if (raw)
            {
                CGEventSetDoubleValueField(cg, kCGScrollWheelEventRawDeltaAxis1, vertical);
                CGEventSetDoubleValueField(cg, kCGScrollWheelEventRawDeltaAxis2, horizontal);
            }
            CGEventSetFlags(cg, shift ? kCGEventFlagMaskShift : 0);
            NSEvent *event = [NSEvent eventWithCGEvent:cg];
            double before_x = [event deltaX], before_y = [event deltaY], x, y;
            macdrv_scroll_wheel_deltas(event, &x, &y);
            if (!continuous && shift && axis == 0 && raw)
            {
                assert(before_x != 0 && before_y == 0);
                assert(x == 0 && y == before_x);
                assert((y > 0) == (vertical > 0));
            }
            else
            {
                assert(x == before_x && y == before_y);
            }
            assert([event deltaX] == before_x && [event deltaY] == before_y);
            assert(([event modifierFlags] & NSEventModifierFlagShift) != 0 == (shift != 0));
            CFRelease(cg);
            checks++;
        }
        printf("shift-wheel: %u AppKit conversion cases passed (no input posted)\n", checks);
    }
    return 0;
}
