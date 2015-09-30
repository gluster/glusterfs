## Readdir-ahead ##

### Summary ###
Provide read-ahead support for directories to improve sequential directory read performance.

### Owners ###
Brian Foster

### Detailed Description ###
The read-ahead feature for directories is analogous to read-ahead for files. The objective is to detect sequential directory read operations and establish a pipeline for directory content. When a readdir request is received and fulfilled, preemptively issue subsequent readdir requests to the server in anticipation of those requests from the user. If sequential readdir requests are received, the directory content is already immediately available in the client. If subsequent requests are not sequential or not received, said data is simply dropped and the optimization is bypassed.

readdir-ahead is currently disabled by default. It can be enabled with the following command:

    gluster volume set <volname> readdir-ahead on
