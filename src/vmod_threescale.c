#include <stdlib.h>
#include <stdio.h>
#include <curl/curl.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vcc_if.h"


int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{
	return (0);
}

int
vmod_authrep(struct sess *sp)
{

	CURL *curl;
	CURLcode res;
	int http_response_code;
	int result = -5;

	char host[] = "http://localhost:3001";
	char operation[] = "/transactions/authrep.xml";	
	char provider_key[] = "provider-key";
	char app_id[] = "app_id";

	const int hits = 1;
	
	// FIXME: this is retarded
	char call[2048];

	sprintf(call,"%s%s?%s=%s&%s=%s&%s=%d&%s",host,operation,"provider_key",provider_key,"app_id",app_id,"usage[hits]",hits,"no_body=true");
	
	curl = curl_easy_init();
  	if(curl) 
	{
    		curl_easy_setopt(curl, CURLOPT_URL, call);
    		res = curl_easy_perform(curl);		
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response_code);
		curl_easy_cleanup(curl);

		if (res==CURLE_OK) 
		{
			
			if (http_response_code==200) {
				result = 0;
				// successful	
			}
			else {
				result = -1;
				// Transaction declined
			}
		}
		else result = -2;
		// connection error
  	}
	else result = -3;
	// curl error

	return result;

}


const char *
vmod_hello(struct sess *sp, const char *name)
{
	char *p;
	unsigned u, v;

	u = WS_Reserve(sp->wrk->ws, 0); /* Reserve some work space */
	p = sp->wrk->ws->f;		/* Front of workspace area */
	v = snprintf(p, u, "Hello from 3scale vmod, %s", name);
	v++;
	if (v > u) {
		/* No space, reset and leave */
		WS_Release(sp->wrk->ws, 0);
		return (NULL);
	}
	/* Update work space with what we've used */
	WS_Release(sp->wrk->ws, v);
	return (p);
}
