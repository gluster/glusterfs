#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

char *stripwhite (char *string);
char *get_token (char **line);
int str2long (char *str, int base, long *l);
int str2ulong (char *str, int base, unsigned long *ul);
int str2int (char *str, int base, int *i);
int str2uint (char *str, int base, unsigned int *ui);
int str2double (char *str, double *d);
int validate_ip_address (char *ip_address);

#endif
