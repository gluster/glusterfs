#include "glusterfsd.h"

int
glusterfsd_getvolume (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_setvolume (struct sock_private *sock_priv)
{
  int ret = 0;
  int remote_errno = 0;
  char readbuf[80] = {0,};
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDMGMT ();
  char *name = data_to_str (dict_get (dict, str_to_data ("ExpVolume")));
  struct xlator *xl = get_xlator_tree_node ();
  FUNCTION_CALLED;
  
  while (xl) {
    if (strcmp (xl->name, name) == 0)
      break;
    xl = xl->next;
  }
  
  if (!xl) {
    ret = -1;
    remote_errno = ENOENT;
  } else {
    if (dict_get (xl->options, str_to_data ("AllowIP"))) {
      // check IP range and decide whether the client can do this or not
      ;
    }
    sock_priv->xl = xl;
  }

  dict_del (dict, str_to_data ("ExpVolume"));

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (remote_errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
handle_mgmt (glusterfsd_fn_t *gmgmtd, struct sock_private *sock_priv)
{
  int ret;
  int operation;
  char readbuf[80] = {0,};
  FILE *fp = sock_priv->fp;

  if (fgets (readbuf, 80, fp) == NULL)
    return -1;
  
  operation = strtol (readbuf, NULL, 0);

  if ((operation < 0) || (operation >= MGMT_MAXVALUE))
    return -1;
  
  ret = gmgmtd[operation].function (sock_priv);

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}
