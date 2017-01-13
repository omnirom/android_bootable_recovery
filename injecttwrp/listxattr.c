#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/xattr.h>

#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>

void print_caps(const char *filename)
{
    struct vfs_cap_data cap_data;
    memset(&cap_data, 0, sizeof(cap_data));
 
    if (getxattr(filename, XATTR_NAME_CAPS, &cap_data, sizeof(cap_data)) < 0) {
        printf("print_caps on %s failed\n", filename);
    }
    else {
        printf("print_caps on %s result:\n", filename);
        printf("     magic_etc=%u \n", cap_data.magic_etc);
        printf("     data[0].permitted=%u \n", (uint32_t) cap_data.data[0].permitted);
        printf("     data[0].inheritable=%u \n", cap_data.data[0].inheritable);
        printf("     data[1].permitted=%u \n", (uint32_t) cap_data.data[1].permitted);
        printf("     data[1].inheritable=%u \n", cap_data.data[1].inheritable);
    }
}

int
main(int argc, char *argv[])
{
   ssize_t buflen, keylen, vallen;
   char *buf, *key, *val;

   if (argc != 2) {
	   fprintf(stderr, "Usage: %s path\n", argv[0]);
	   exit(EXIT_FAILURE);
   }

   /*
	* Determine the length of the buffer needed.
	*/
   buflen = listxattr(argv[1], NULL, 0);
   if (buflen == -1) {
	   perror("listxattr");
	   exit(EXIT_FAILURE);
   }
   if (buflen == 0) {
	   printf("%s has no attributes.\n", argv[1]);
	   exit(EXIT_SUCCESS);
   }

   /*
	* Allocate the buffer.
	*/
   buf = malloc(buflen);
   if (buf == NULL) {
	   perror("malloc");
	   exit(EXIT_FAILURE);
   }

   /*
	* Copy the list of attribute keys to the buffer.
	*/
   buflen = listxattr(argv[1], buf, buflen);
   if (buflen == -1) {
	   perror("listxattr");
	   exit(EXIT_FAILURE);
   }

   /*
	* Loop over the list of zero terminated strings with the
	* attribute keys. Use the remaining buffer length to determine
	* the end of the list.
	*/
   key = buf;
   while (buflen > 0) {

	   /*
		* Output attribute key.
		*/
	   printf("%s: ", key);

	   /*
		* Determine length of the value.
		*/
	   vallen = getxattr(argv[1], key, NULL, 0);
	   if (vallen == -1)
		   perror("getxattr");

	   if (vallen > 0) {

		   /*
			* Allocate value buffer.
			* One extra byte is needed to append 0x00.
			*/
		   val = malloc(vallen + 1);
		   if (val == NULL) {
			   perror("malloc");
			   exit(EXIT_FAILURE);
		   }

		   /*
			* Copy value to buffer.
			*/
		   vallen = getxattr(argv[1], key, val, vallen);
		   if (vallen == -1)
			   perror("getxattr");
		   else {
			   /*
				* Output attribute value.
				*/
			   val[vallen] = 0;
			   printf("%s", val);
		   }

		   free(val);
	   } else if (vallen == 0)
		   printf("<no value>");

	   printf("\n");

	   /*
		* Forward to next attribute key.
		*/
	   keylen = strlen(key) + 1;
	   buflen -= keylen;
	   key += keylen;
   }

   free(buf);
   print_caps(argv[1]);
   exit(EXIT_SUCCESS);
}
