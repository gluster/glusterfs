#include <fnmatch.h>
#include "authenticate.h"
#define IP_DELIMITER " ,"

auth_result_t gf_auth (dict_t *input_params, dict_t *config_params)
{
  char *name = NULL;
  char *searchstr = NULL;
  data_t *allow_ip = NULL, *reject_ip = NULL;
  char *peer = NULL;

  name = data_to_str (dict_get (input_params, "remote-subvolume"));
  if (!name) {
    gf_log ("authenticate/ip",
	    GF_LOG_ERROR,
	    "remote-subvolume not specified");
    return AUTH_REJECT;
  }
  
  asprintf (&searchstr, "auth.ip.%s.allow", name);
  allow_ip = dict_get (config_params,
		       searchstr);
  free (searchstr);
  
  peer = data_to_str (dict_get (input_params, "peer"));
  if (!peer) {
    gf_log ("auth/ip",
	    GF_LOG_ERROR,
	    "peer not specified");
    return AUTH_REJECT;
  }

  if (reject_ip) {
    char *ip_addr_str = NULL;
    char *tmp;
    char *ip_addr_cpy = strdup (reject_ip->data);
      
    ip_addr_str = strtok_r (ip_addr_cpy, IP_DELIMITER, &tmp);
    
    while (ip_addr_str) {
      char negate = 0,  match =0;
      gf_log (name,  GF_LOG_DEBUG,
	      "rejected = \"%s\", received ip addr = \"%s\"",
	      ip_addr_str, peer);
      if (ip_addr_str[0] == '!') {
	negate = 1;
	ip_addr_str++;
      }

      match = fnmatch (ip_addr_str,
		       peer,
		       0);
      if (negate ? match : !match) {
	free (ip_addr_cpy);
	return AUTH_REJECT;
      }
      ip_addr_str = strtok_r (NULL, IP_DELIMITER, &tmp);
    }
    free (ip_addr_cpy);
  }      

  if (allow_ip) {
    char *ip_addr_str = NULL;
    char *tmp;
    char *ip_addr_cpy = strdup (allow_ip->data);
    
    ip_addr_str = strtok_r (ip_addr_cpy, IP_DELIMITER, &tmp);
      
    while (ip_addr_str) {
      char negate = 0, match = 0;
      gf_log (name,  GF_LOG_DEBUG,
	      "allowed = \"%s\", received ip addr = \"%s\"",
	      ip_addr_str, peer);
      if (ip_addr_str[0] == '!') {
	negate = 1;
	ip_addr_str++;
      }

      match = fnmatch (ip_addr_str,
		       peer,
		       0);

      if (negate ? match : !match) {
	free (ip_addr_cpy);
	return AUTH_ACCEPT;
      }
      ip_addr_str = strtok_r (NULL, IP_DELIMITER, &tmp);
    }
    free (ip_addr_cpy);
  }      
  
  asprintf (&searchstr, "auth.ip.%s.reject", name);
  reject_ip = dict_get (config_params,
			searchstr);
  free (searchstr);
  return AUTH_DONT_CARE;
}
