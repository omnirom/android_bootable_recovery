#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv)
{
    unsigned n;
    unsigned char *x;
    unsigned m;
    unsigned run_val;
    unsigned run_count;
 
    n = gimp_image.width * gimp_image.height;
    m = 0;
    x = gimp_image.pixel_data;

    printf("struct {\n");
    printf("  unsigned width;\n");
    printf("  unsigned height;\n");
    printf("  unsigned cwidth;\n");
    printf("  unsigned cheight;\n");
    printf("  unsigned char rundata[];\n");
    printf("} font = {\n");
    printf("  .width = %d,\n  .height = %d,\n  .cwidth = %d,\n  .cheight = %d,\n", gimp_image.width, gimp_image.height,
           gimp_image.width / 96, gimp_image.height);
    printf("  .rundata = {\n");
   
    run_val = (*x ? 0 : 255);
    run_count = 1;
    n--;
    x+=3;

    while(n-- > 0) {
        unsigned val = (*x ? 0 : 255);
        x+=3;
        if((val == run_val) && (run_count < 127)) {
            run_count++;
        } else {
eject:
            printf("0x%02x,",run_count | (run_val ? 0x80 : 0x00));
            run_val = val;
            run_count = 1;
            m += 5;
            if(m >= 75) {
                printf("\n");
                m = 0;
            }
        }
    }
    printf("0x%02x,",run_count | (run_val ? 0x80 : 0x00));
    printf("\n0x00,");
    printf("\n");
    printf("  }\n};\n");
    return 0;
}
