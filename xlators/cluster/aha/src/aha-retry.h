#ifndef _AHA_RETRY_H
#define _AHA_RETRY_H

void aha_retry_failed_fops (struct aha_conf *conf);

void aha_retry_fop (struct aha_fop *fop);

void aha_force_unwind_fops (struct aha_conf *conf);

void aha_force_unwind_fop (struct aha_fop *fop);

#endif /* _AHA_RETRY_H */
