#ifndef __ANDROID_UTILS_H__
#define __ANDROID_UTILS_H__

#ifndef MAJOR
/* FIXME:  I am using illicit insider knowledge of
 * kernel major/minor representation...  */
#define MAJOR(dev) (((dev)>>8)&0xff)
#define MINOR(dev) ((dev)&0xff)
#endif

#endif /* !__ANDROID_UTILS_H__ */

