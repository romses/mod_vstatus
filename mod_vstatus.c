#include "apr.h"
#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_version.h"
#include "apu_version.h"
#include "apr_want.h"
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_connection.h"
#include "http_request.h"
#include "util_script.h"
#include "ap_mpm.h"
#include "ver.h"
#include "favicon.h"

module AP_MODULE_DECLARE_DATA vstatus_module;

//#define DEBUG 1

#ifndef DEFAULT_TIME_FORMAT
	#define DEFAULT_TIME_FORMAT "%A, %d-%b-%Y %H:%M:%S %Z"
#endif

#define TIMESTAMP_TIME_FORMAT "%s"
#define SHM_FILENAME "VSTATUS_SHM"
#define MOD_COPYRIGHT_STRING "<HR><I>mod_vstatus "VERSION" -- Christopher Kreitz &lt;c.kreitz@macrocom.de&gt; -- <a href=\"http://www.macrocom.de/\">http://www.macrocom.de</a></i>" 
#define CODES_TRACKED 47

static const int vstatus_tracked_status[] = {
	0,1,2,3,4,5,
	100,101,
	200,201,202,203,204,205,206,
	300,301,302,303,304,305,306,307,
	400,401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,
	500,501,502,503,504,505
};

static const char* help[] = {
	"sec10","sec10.1","sec10.2","sec10.3","sec10.4","sec10.5",
	"sec10.1.1","sec10.1.",
	"sec10.2.1","sec10.2.2","sec10.2.3","sec10.2.4","sec10.2.5","sec10.2.6","sec10.2.7",
	"sec10.3.1","sec10.3.2","sec10.3.3","sec10.3.4","sec10.3.5","sec10.3.6","sec10.3.7","sec10.3.8",
	"sec10.4.1","sec10.4.2","sec10.4.3","sec10.4.4","sec10.4.5","sec10.4.6","sec10.4.7","sec10.4.8","sec10.4.9","sec10.4.10","sec10.4.11","sec10.4.12","sec10.4.13","sec10.4.14","sec10.4.15","sec10.4.16","sec10.4.17","sec10.4.18",
	"sec10.5.1","sec10.5.2","sec10.5.3","sec10.5.4","sec10.5.5","sec10.5.6"
};

typedef struct vstatus_s{
	char *hostname;
	apr_uint32_t resultcode[CODES_TRACKED];	//Count Requests/code
}vstatus_data;

typedef struct {
	int timestamp;
	vstatus_data *data;
} vstatus_ringbuffer;

typedef struct {
	apr_hash_t* filter;
	apr_hash_t* format;
	apr_hash_t* delta;
	apr_hash_t* type;
	apr_hash_t* comment;
	apr_pool_t* pool;
	int histSize;
	int granularity;
} vstatus_cfg;

volatile int *bucket;
int *old_bucket;
apr_shm_t *shm;
vstatus_ringbuffer *rbuffer;
vstatus_cfg *gconf;
static int num_vhosts = 0;

int handle_html(request_rec * r,int rel,int delta,int dump);
int handle_json(request_rec * r,int rel,int delta,int dump);
int handle_csv(request_rec * r,int rel,int delta,int dump);
int handle_google(request_rec * r);
int handle_else(request_rec * r);
apr_status_t apr_atomic_init(apr_pool_t *p);
apr_uint32_t apr_atomic_inc32(volatile apr_uint32_t *mem);
apr_uint32_t apr_atomic_read32(volatile apr_uint32_t *mem);
void apr_atomic_set32(volatile apr_uint32_t *mem,apr_uint32_t val);
void apr_hash_this(apr_hash_index_t *hi, const void **key, apr_ssize_t *klen, void **val);

unsigned long hostindex(char *str){
	int i;

	if(str==NULL)
		return 0;

	for (i=0;i<num_vhosts;i++){
		if(strcmp(str,rbuffer[0].data[i].hostname)==0){
			return i;		//Count for Domainname
		}
	}
	return 1;		//Count for IP/Servername
}

static int logRequest(request_rec * r){
	int c=0;
	unsigned int loc=0;

	loc=hostindex(r->server->server_hostname);
#ifdef DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,"mod_reqstatus : *bucket = %u",*bucket);
#endif

	for (c=0; c < CODES_TRACKED; c++)   {
		if (vstatus_tracked_status[c] == r->status) {
#ifdef DEBUG
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,"mod_reqstatus : c = %u",c);
#endif
			apr_atomic_inc32(&rbuffer[*bucket].data[loc].resultcode[c]);
			apr_atomic_inc32(&rbuffer[*bucket].data[0].resultcode[c]);
		}
	}

	apr_atomic_inc32(&rbuffer[*bucket].data[0].resultcode[r->status/100]);
	apr_atomic_inc32(&rbuffer[*bucket].data[loc].resultcode[r->status/100]);

	apr_atomic_inc32(&rbuffer[*bucket].data[0].resultcode[0]);
	apr_atomic_inc32(&rbuffer[*bucket].data[loc].resultcode[0]);

	return DECLINED;
}

int getCounter(int code){
	int i;
	for(i=0;i<CODES_TRACKED;i++){
		if(code == vstatus_tracked_status[i]){
			return i;
		}
	}
	return 0;
}

apr_time_t getTime(apr_time_t time){
	return (apr_time_sec(time)/gconf->granularity);
}

int handle(request_rec * r){
	char *format;
	char *key = r->path_info;
	int delta,rel;
//	char *val;
	apr_uint32_t *val;
	apr_time_t time = getTime(r->request_time);

	if(key==NULL){
		key="";
	}else{
		if(key[0]=='/'){
			*key++; // remove foremost slash
		}
	}

	if(apr_atomic_read32(&rbuffer[*bucket].timestamp)<time){

		apr_atomic_inc32(bucket);
//		apr_atomic_cas32(bucket,0,gconf->histSize);				//Resets the bucket to zero, if max size is reached
//		if(*bucket>=gconf->histSize){
//			ap_log_rerror(APLOG_MARK, APLOG_EMERG, 0, r,"Bucket exeeded Range!: %i (%i)",*bucket,gconf->histSize);
//			apr_atomic_set32(bucket,0);							//Emergency handler
//		}
		*bucket%=gconf->histSize;
		apr_atomic_set32(&rbuffer[*bucket].timestamp,getTime(r->request_time));

		memcpy(&rbuffer[*bucket].data[0],&rbuffer[*old_bucket].data[0],num_vhosts*sizeof(vstatus_data));
		apr_atomic_set32(old_bucket,*bucket);
	}

	if (strncmp(r->handler, "vstatus",7) != 0) {
		return DECLINED;
	}

	format= (char*)apr_hash_get (gconf->format, key, strlen(key));
	if(format==NULL){
		format="else";
	}

	val=(int*)apr_hash_get(gconf->delta,key,strlen(key));
	if(val==NULL){
		delta=1;
	}else{
		delta=atoi(val);
	}

	char* t = (char*)apr_hash_get (gconf->type, key, strlen(key));
	if((t==NULL)||(strcmp(t,"rel")==0)){
		rel=1;
	}else{
		rel=0;
	}



//	if (strcmp(r->handler, "vstatus") == 0) {

		if(strcmp(r->path_info,"/status.html")==0){
			return print_status_page(r);
		}
		ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r,"mod_vstatus: FILENAME: **%s**",r->path_info);

		if(strcmp(format,"json")==0){
			return handle_json(r,rel,delta,0);
		}else if(strcmp(format,"csv")==0){
			return handle_csv(r,rel,delta,0);
		}else if(strcmp(format,"html")==0){
			return handle_html(r,rel,delta,0);
		}else if(strcmp(format,"google")==0){
			return handle_google(r);
		}else if(strcmp(format,"dump-csv")==0){
			return handle_csv(r,rel,delta,1);
		}else if(strcmp(format,"dump-json")==0){
			return handle_json(r,rel,delta,1);
		}else if(strcmp(format,"dump-html")==0){
			return handle_html(r,rel,delta,1);
		}else{
			return handle_else(r);
		}
//	}
	return DECLINED;
}

int getDeltaPos(int pos, int delta){
	int i=0;
	int deltapos=(((pos-(delta/gconf->granularity))+gconf->histSize)%gconf->histSize)-1;
        for(i=0;i<gconf->histSize;i++){
                deltapos++;
                deltapos%=gconf->histSize;
                if(rbuffer[deltapos].timestamp!=0){
                        if((rbuffer[(pos+gconf->histSize-1)%gconf->histSize].timestamp-delta - rbuffer[deltapos].timestamp)<= (delta/gconf->granularity)){
                                i=gconf->histSize;
                        }
                }else{
//			i=gconf->histSize;
			deltapos=pos;
			break;
		}
        }
	return deltapos;
}

int print_status_page(request_rec * r){
	int i=0;
	ap_set_content_type(r, "text/html");
	ap_rputs(DOCTYPE_HTML_3_2"<html>\n<head>\n<title>mod_vstatus: internal config</title>\n",r);
	ap_rputs(FAVICON,r);
	ap_rputs("<body>\n",r);
	ap_rputs("<h1>mod_vstatus</h1><h2>mod_vstatus: internal config</h2>", r);
	ap_rputs("<hline>\n",r);
	ap_rputs("<h2>Hosttable:</h2>", r);
	ap_rputs("<table border=1>", r);
	ap_rputs("<tr>", r);
	ap_rputs("<th>", r);
	ap_rputs("1", r);
	ap_rputs("</th>", r);
	ap_rputs("<th>", r);
	ap_rputs("2", r);
	ap_rputs("</th>", r);
	ap_rputs("</tr>", r);
	for (i=0;i<num_vhosts;i++){
		ap_rputs("<tr>", r);
		ap_rputs("<td>", r);
		ap_rputs("3", r);
		ap_rputs("</td>", r);
		ap_rputs("<td>", r);
		ap_rputs((char *) apr_psprintf(r->pool, "%s",rbuffer[0].data[i].hostname), r);
//rbuffer[0].data[i].hostname
		ap_rputs("</td>", r);
		ap_rputs("</tr>", r);
	}
	ap_rputs("</table>", r);

	ap_rputs("</body>\n",r);
	ap_rputs("</html>\n",r);
	return OK;
}

int handle_else(request_rec * r){
	apr_hash_index_t *hi;
	int i;
	char* key;
	char* val;
	char* format;
	int delta;
	char* type;

	ap_set_content_type(r, "text/html");
	ap_rputs(DOCTYPE_HTML_3_2"<html>\n<head>\n<title>mod_vstatus: detail</title>\n",r);
	ap_rputs(FAVICON,r);
	ap_rputs("<body>\n",r);
	ap_rputs("<h1>mod_vstatus</h1><h2>HTTP responses for ", r);
	ap_rputs((char *) apr_psprintf(r->pool,	"%s",r->server->server_hostname),r);
	ap_rputs("</h2>\n",r);
	ap_rvputs(r, "<dt>Current Time: ",ap_ht_time(r->pool, apr_time_now(), DEFAULT_TIME_FORMAT, 0),"</dt>\n", NULL);
	ap_rputs((char *) apr_psprintf(r->pool,	"Number of Filters: %i",apr_hash_count (gconf->format)),r);

	ap_rputs("<hr>\n",r);
	ap_rputs("Global Options:<br>\n",r);
	ap_rputs((char *) apr_psprintf(r->pool, "History size: %i<br>\n",gconf->histSize),r);
	ap_rputs((char *) apr_psprintf(r->pool, "Bucket: %i <br><BR>\n",*bucket),r);

	ap_rputs("The following Filters are defined:<br>\n",r);
	ap_rputs("<table border=1>",r);
	ap_rputs("<tr><th>Filter</th><th>Format</th><th>Delta</th><th>Type</th><th>Comment</th></tr>",r);
	ap_rputs("<hr>\n",r);
	for (hi = apr_hash_first(r->pool, gconf->format); hi; hi = apr_hash_next(hi)) {
		apr_hash_this(hi, &key, NULL, &val);

		format= (char*)apr_hash_get (gconf->format, key, strlen(key));
		if(format==NULL){
			format="else";
		}

		val=(int*)apr_hash_get(gconf->delta,key,strlen(key));
		if(val==NULL){
			delta=1;
		}else{
			delta=atoi(val);
		}

		type = (char*)apr_hash_get (gconf->type, key, strlen(key));
		if(type==NULL){
			type="";
		}

		char* comment = (char*)apr_hash_get (gconf->comment, key, strlen(key));
		if(comment==NULL){
			comment="";
		}


		ap_rputs("<tr>\n",r);
		ap_rputs((char *) apr_psprintf(r->pool, "<td><a href=%s/%s>%s</a></td>\n",r->uri,key,key),r);
		ap_rputs((char *) apr_psprintf(r->pool, "<td>%s</td>\n",format),r);
		ap_rputs((char *) apr_psprintf(r->pool, "<td>%i</td>\n",delta),r);
		ap_rputs((char *) apr_psprintf(r->pool, "<td>%s</td>\n",type),r);
		ap_rputs((char *) apr_psprintf(r->pool, "<td>%s</td>\n",comment),r);
		ap_rputs("</tr>\n",r);
	}
	ap_rputs("</table>",r);
	ap_rputs(MOD_COPYRIGHT_STRING,r);
	ap_rputs("</body>",r);
	ap_rputs("</html>",r);
	return OK;
}

int handle_html(request_rec * r,int rel,int delta,int dump){

	char* key;
	apr_ssize_t klen;
	apr_array_header_t* val;
	apr_hash_index_t *index;
	const int *codes;
	int num_codes;
	int i,j,k;

	char *f = r->path_info;
	*f++; // remove foremost slash

	int pos=((*old_bucket-1)+gconf->histSize)%gconf->histSize;
	int deltapos=getDeltaPos(pos,delta);

	char* type = (char*)apr_hash_get (gconf->type, f, strlen(f));

	if(type==NULL){
		type="rel";
	}

	val = apr_hash_get (gconf->filter, f, strlen(f));
	char* fmt = apr_hash_get (gconf->format ,f, strlen(f));

	if(val==NULL){
		codes=vstatus_tracked_status;
		num_codes=CODES_TRACKED;
	}else{
		codes=(int*)val->elts;
		num_codes=val->nelts;
	}


	ap_set_content_type(r, "text/html");
	ap_rputs(DOCTYPE_HTML_3_2"<html>\n<head>\n<title>mod_vstatus: detail</title>\n</head>\n",r);
	ap_rputs("<body>\n",r);
	ap_rputs("<h1>mod_vstatus</h1><h2>HTTP responses for ", r);
	ap_rputs((char *) apr_psprintf(r->pool,	"%s",r->server->server_hostname),r);
	ap_rputs("</h2>\n",r);
	ap_rvputs(r, "<dt>Current Time: ",ap_ht_time(r->pool, apr_time_now(), DEFAULT_TIME_FORMAT, 0),"</dt>\n", NULL);
	ap_rputs("<hr>\n",r);

#ifdef DEBUG
	ap_rputs((char *) apr_psprintf(r->pool, "pos     : %i %"APR_INT64_T_FMT"<br>",pos,rbuffer[pos].timestamp),r);
	ap_rputs((char *) apr_psprintf(r->pool, "deltapos: %i %"APR_INT64_T_FMT"<br>",deltapos,rbuffer[deltapos].timestamp),r);
	ap_rputs((char *) apr_psprintf(r->pool, "delta   : %"APR_INT64_T_FMT" - %"APR_INT64_T_FMT" = %"APR_INT64_T_FMT"<br>",
			rbuffer[pos].timestamp,
			rbuffer[deltapos].timestamp,
			(rbuffer[pos].timestamp-rbuffer[deltapos].timestamp)*gconf->granularity
	),r);
	ap_rputs((char *) apr_psprintf(r->pool, "Time    : %i<br>",getTime(r->request_time)),r);
	ap_rputs((char *) apr_psprintf(r->pool, "Format  : %s<br>",fmt),r);
	ap_rputs((char *) apr_psprintf(r->pool, "Gran: %i Hist %i<br>",gconf->granularity,gconf->histSize),r);
	ap_rputs((char *) apr_psprintf(r->pool, "r->time : %"APR_INT64_T_FMT"<br>",apr_time_sec(r->request_time)),r);
#endif
	ap_rputs("<table border = \"1\">\n",r);
	ap_rputs("<tr>\n",r);


//Headline
	ap_rputs("<td>Host</td>\n",r);
#ifdef DEBUG
	ap_rputs("<td>Time</td>\n",r);
#endif
	for(i=0;i<num_codes;i++){
		switch (vstatus_tracked_status[getCounter(codes[i])]){
			case 0:
				ap_rputs("<td>Total</td>",r);
				break;
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
				ap_rputs((char *) apr_psprintf(r->pool, "<td><a href=\"http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html#%s\">%ixx</a></td>",help[getCounter(codes[i])],vstatus_tracked_status[getCounter(codes[i])]),r);
				break;
			default:
				ap_rputs((char *) apr_psprintf(r->pool, "<td><a href=\"http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html#%s\">%i</a></td>",help[getCounter(codes[i])],vstatus_tracked_status[getCounter(codes[i])]),r);
		}
	}
	ap_rputs("</tr>\n<tr>\n",r);
//Summarized Data
	ap_rputs((char *) apr_psprintf(r->pool, "<td>%s</td>","Total"),r);
#ifdef DEBUG
	ap_rputs((char *) apr_psprintf(r->pool, "<td>%i</td>",rbuffer[*old_bucket].timestamp),r);
#endif
	for(j=0;j<num_codes;j++){
		if(strcmp(type,"abs")){
			ap_rputs((char *) apr_psprintf(r->pool, "<td>%li</td>",(rbuffer[pos].data[0].resultcode[getCounter(codes[j])]
									      -rbuffer[deltapos].data[0].resultcode[getCounter(codes[j])])
			),r);
		}else{
			ap_rputs((char *) apr_psprintf(r->pool, "<td>%li</td>",rbuffer[*old_bucket].data[0].resultcode[getCounter(codes[j])]),r);
        apr_array_header_t* val;
		}
	}
	ap_rputs("</tr>\n",r);
//Individual Data
	for(i=1;i<num_vhosts;i++){
		ap_rputs("<tr>\n",r);
		ap_rputs((char *) apr_psprintf(r->pool, "<td><a href=http://%s>%s</a></td>",rbuffer[*bucket].data[i].hostname,rbuffer[0].data[i].hostname),r);
#ifdef DEBUG
		ap_rputs((char *) apr_psprintf(r->pool, "<td>%i</td>",(rbuffer[pos].timestamp-rbuffer[deltapos].timestamp)*gconf->granularity),r);
#endif
		for(j=0;j<num_codes;j++){
			if(strcmp(type,"abs")){
				ap_rputs((char *) apr_psprintf(r->pool, "<td>%li</td>\n",(rbuffer[pos].data[i].resultcode[getCounter(codes[j])]
											-rbuffer[deltapos].data[i].resultcode[getCounter(codes[j])])
	                        ),r);
			}else{
				ap_rputs((char *) apr_psprintf(r->pool, "<td>%li</td>\n",rbuffer[*old_bucket].data[i].resultcode[getCounter(codes[j])]),r);
			}
		}
	}
	ap_rputs("</tr>\n",r);
	ap_rputs("</table>\n",r);
	ap_rputs(MOD_COPYRIGHT_STRING,r);
	ap_rputs("</body>\n",r);
	return OK;
}
int handle_csv(request_rec * r,int rel,int delta,int dump){
	char* key;
	apr_ssize_t klen;
	apr_array_header_t* val;
	apr_hash_index_t *index;
	int num_codes;
	int i,j,k,iter;
	int t,p,pos,deltapos;
	int iter_max;
	const int *codes;

	char *f = r->path_info;
	*f++; // remove foremost slash

	if(dump==1){
		p=*bucket;
		iter_max=gconf->histSize;
		rel=0;
		codes=vstatus_tracked_status;
		num_codes=CODES_TRACKED;
	}else{
		iter_max=1;
		val = apr_hash_get (gconf->filter, f, strlen(f));
		if(val==NULL){
			codes=vstatus_tracked_status;
			num_codes=CODES_TRACKED;
		}else{
			codes=(int*)val->elts;
			num_codes=val->nelts;
		}
		if(rel==1){
			p=*bucket-1;
		}else{
			p=*bucket;
		}
	}
	
	ap_set_content_type(r, "text/plain");

	//Headline
	ap_rputs("Time,Host",r);
	for(i=0;i<num_codes;i++){
		switch (vstatus_tracked_status[getCounter(codes[i])]){
			case 0:
				ap_rputs(",Total",r);
				break;
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
				ap_rputs((char *) apr_psprintf(r->pool, ",%ixx",vstatus_tracked_status[getCounter(vstatus_tracked_status[i])]),r);
				break;
			default:
				ap_rputs((char *) apr_psprintf(r->pool, ",%i",vstatus_tracked_status[getCounter(vstatus_tracked_status[i])]),r);
		}
	}
	ap_rputs("\n",r);
	



	for(iter=0;iter<iter_max;iter++){
		pos=(p-iter+gconf->histSize)%gconf->histSize;
//		deltapos=((pos-delta)+gconf->histSize)%gconf->histSize;
		deltapos=getDeltaPos(pos,delta);
		for(i=0;i<num_vhosts;i++){
			t=(iter==0)?apr_time_sec(r->request_time) : rbuffer[pos].timestamp*gconf->granularity;

			ap_rputs((char *) apr_psprintf(r->pool, "%i,%s",t,rbuffer[pos].data[i].hostname),r);
			for(j=0;j<num_codes;j++){
				if(rel==0){
					ap_rputs((char *) apr_psprintf(r->pool, ",%li",rbuffer[pos].data[i].resultcode[getCounter(codes[j])]),r);
				}else{
					ap_rputs((char *) apr_psprintf(r->pool, ",%li",rbuffer[pos].data[i].resultcode[getCounter(codes[j])]
																 -rbuffer[deltapos].data[i].resultcode[getCounter(codes[j])]),r);
				}
			}
			ap_rputs("\n",r);
		}
//ap_rputs("\n",r);
	}
	return OK;
}

int handle_json(request_rec * r,int rel,int delta,int dump){
	char* key;
	apr_ssize_t klen;
	apr_array_header_t* val;
	apr_hash_index_t *index;
	int num_codes;
	int i,j,k,iter;
	int t,p,pos,deltapos;
	int iter_max;
	const int *codes;

	char *f = r->path_info;
	*f++; // remove foremost slash

	if(dump==1){
		p=*bucket;
		iter_max=gconf->histSize;
		rel=0;
		codes=vstatus_tracked_status;
		num_codes=CODES_TRACKED;
	}else{
		iter_max=1;
		val = apr_hash_get (gconf->filter, f, strlen(f));
		if(val==NULL){
			codes=vstatus_tracked_status;
			num_codes=CODES_TRACKED;
		}else{
			codes=(int*)val->elts;
			num_codes=val->nelts;
		}
		if(rel==1){
			p=*bucket-1;
		}else{
			p=*bucket;
		}
	}

	ap_set_content_type(r, "text/plain");

	ap_rputs("{\n",r);

	for(iter=0;iter<iter_max;iter++){
		pos=(p-iter+gconf->histSize)%gconf->histSize;
//		deltapos=((pos-delta)+gconf->histSize)%gconf->histSize;
		deltapos=getDeltaPos(pos,delta);
		t=rbuffer[pos].timestamp*gconf->granularity;

		if(iter!=0){
				ap_rputs(",",r);
		}

		ap_rputs((char *) apr_psprintf(r->pool, "\"%li\":{",t),r);	//Zeit

		for(i=0;i<num_vhosts;i++){
			if(i!=0){
				ap_rputs(",",r);
			}
			ap_rputs((char *) apr_psprintf(r->pool, "\"%s\":{",rbuffer[pos].data[i].hostname),r);		//Hostname
			for(j=0;j<num_codes;j++){
				if(j!=0){
					ap_rputs(",",r);
				}
				if(rel==0){
					ap_rputs((char *) apr_psprintf(r->pool, "\"%i\":\"%li\"",codes[j],rbuffer[pos].data[i].resultcode[getCounter(codes[j])]),r);
				}else{
					ap_rputs((char *) apr_psprintf(r->pool, "\"%i\":\"%li\"",codes[j],(long int)rbuffer[pos].data[i].resultcode[getCounter(codes[j])]-
													(long int)rbuffer[deltapos].data[i].resultcode[getCounter(codes[j])]),r);
				}
			}
			ap_rputs("}\n",r);
		}
		ap_rputs("}\n",r);
	}
	ap_rputs("}\n",r);
	return OK;
}

int handle_google(request_rec * r){

	char* key;
	apr_ssize_t klen;
	apr_array_header_t* val;
	apr_hash_index_t *index;
	int *codes;
	int num_codes;
	int i,j,k;

	char *f = r->path_info;
	*f++; // remove foremost slash

	int delta=apr_hash_get(gconf->delta,f,strlen(f));
	if(delta==0){
		delta=1;
	}

	int pos=((*old_bucket-1)+gconf->histSize)%gconf->histSize;
//	int deltapos=((*old_bucket-(delta+1))+gconf->histSize)%gconf->histSize;
	int deltapos=getDeltaPos(pos,delta);

	while(rbuffer[(pos+gconf->histSize-1)%gconf->histSize].timestamp-delta>rbuffer[deltapos].timestamp){
		deltapos++;
		deltapos%=gconf->histSize;
	}

	char* type = (char*)apr_hash_get (gconf->type, f, strlen(f));

	if(type==NULL){
		type="rel";
	}

	val = apr_hash_get (gconf->filter, f, strlen(f));
	char* fmt = apr_hash_get (gconf->format ,f, strlen(f));

	if(val==NULL){
		codes=vstatus_tracked_status;
		num_codes=CODES_TRACKED;
	}else{
		codes=(int*)val->elts;
		num_codes=val->nelts;
	}


	ap_set_content_type(r, "text/plain");
	ap_rputs("{\"cols\":[\n",r);

//Headline
	ap_rputs("{\"id\":\"\",\"label\":\"Host\",\"pattern\":\"\",\"type\":\"string\"}\n",r);

	for(i=0;i<num_codes;i++){
		switch (vstatus_tracked_status[getCounter(codes[i])]){
			case 0:
				ap_rputs(",{\"id\":\"\",\"label\":\"Total\",\"pattern\":\"\",\"type\":\"number\"}\n",r);
				break;
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
				ap_rputs((char *) apr_psprintf(r->pool, ",{\"id\":\"\",\"label\":\"%ixx\",\"pattern\":\"\",\"type\":\"number\"}\n",vstatus_tracked_status[getCounter(codes[i])]),r);
				break;
			default:
				ap_rputs((char *) apr_psprintf(r->pool, ",{\"id\":\"\",\"label\":\"%i\",\"pattern\":\"\",\"type\":\"number\"}\n",vstatus_tracked_status[getCounter(codes[i])]),r);
		}
	}
	ap_rputs("],\n",r);
	ap_rputs("\"rows\":[\n",r);
//Summarized Data
	ap_rputs("{\"c\":[{\"v\":\"Total\",\"f\":null}",r);
//	ap_rputs("{\"c\":[{\"v\":\"\",\"f\":null}",r);

	for(j=0;j<num_codes;j++){
		if(strcmp(type,"abs")){
//			ap_rputs((char *) apr_psprintf(r->pool, ",{\"v\":%i,\"f\":null}",0),r);
			ap_rputs((char *) apr_psprintf(r->pool, ",{\"v\":%li,\"f\":null}",((long int)rbuffer[pos].data[0].resultcode[getCounter(codes[j])]
									      -(long int)rbuffer[deltapos].data[0].resultcode[getCounter(codes[j])])
			),r);
		}else{
			ap_rputs((char *) apr_psprintf(r->pool, ",{\"v\":%li,\"f\":null}",(long int)rbuffer[*old_bucket].data[0].resultcode[getCounter(codes[j])]),r);
//			ap_rputs((char *) apr_psprintf(r->pool, ",{\"v\":%i,\"f\":null}",0),r);
		}
	}
	ap_rputs("]}\n",r);
//Individual Data
	for(i=1;i<num_vhosts;i++){
		ap_rputs((char *) apr_psprintf(r->pool, ",{\"c\":[{\"v\":\"%s\",\"f\":null}",rbuffer[0].data[i].hostname),r);

		for(j=0;j<num_codes;j++){
			if(strcmp(type,"abs")){
				ap_rputs((char *) apr_psprintf(r->pool, ",{\"v\":%li,\"f\":null}",((long int)rbuffer[pos].data[i].resultcode[getCounter(codes[j])]
											-(long int)rbuffer[deltapos].data[i].resultcode[getCounter(codes[j])])
	                        ),r);
			}else{
				ap_rputs((char *) apr_psprintf(r->pool, ",{\"v\":%li,\"f\":null}",(long int)rbuffer[*old_bucket].data[i].resultcode[getCounter(codes[j])]),r);
			}
		}
		ap_rputs("]}\n",r);
	}
	ap_rputs("]}\n",r);
	return OK;
}

static void *init_vstatus_cfg(apr_pool_t* pool, server_rec* s){
	apr_status_t rv;

	vstatus_cfg *cfg = apr_pcalloc(pool, sizeof(vstatus_cfg));
	cfg->pool=pool;
	cfg->filter=apr_hash_make(pool);
	cfg->comment=apr_hash_make(pool);
	cfg->format=apr_hash_make(pool);
	cfg->type=apr_hash_make(pool);
	cfg->delta=apr_hash_make(pool);
	cfg->histSize=15;		//log 15 entries
	cfg->granularity=60;		//1 entry per minute

	gconf=cfg;
	return (void*)cfg;
}

static const char* setFilter(cmd_parms* cmd, void* cfg, const char *filter, const char* value ){
//static const char* setFilter(cmd_parms* cmd, vstatus_cfg* cfg, const char *filter, const char* value ){

	apr_array_header_t* attrs = apr_hash_get(gconf->filter, filter, APR_HASH_KEY_STRING);
	int* attr;

	if (!attrs) {
		attrs = apr_array_make(gconf->pool, 2, sizeof(int)) ;
		apr_hash_set(gconf->filter, filter, APR_HASH_KEY_STRING, attrs);
	}

	attr = apr_array_push(attrs);
	*attr = atoi(value);

	return NULL;
}

static const char* setDelta(cmd_parms* cmd, void* cfg,const char *filter, const char* type ){
	apr_hash_set(gconf->delta, filter, APR_HASH_KEY_STRING, type);
	return NULL;
}

static const char* setHistSize(cmd_parms* cmd, void* cfg, const char* val ){
//static const char* setHistSize(cmd_parms* cmd, vstatus_cfg* cfg, const char* val ){
	gconf->histSize=atoi(val);
	return NULL;
}

static const char* setGranularity(cmd_parms* cmd, void* cfg,const char* val ){
	gconf->granularity=atoi(val);
	return NULL;
}

static const char* setFormat(cmd_parms* cmd, void* cfg,const char *filter, const char* type ){
	apr_hash_set(gconf->format, filter, APR_HASH_KEY_STRING, type);
	return NULL;
}

static const char* setType(cmd_parms* cmd, void* cfg,const char *filter, const char* type ){
	apr_hash_set(gconf->type, filter, APR_HASH_KEY_STRING, type);
	return NULL;
}

static const char* setComment(cmd_parms* cmd, void* cfg,const char *filter, const char* type ){
	apr_hash_set(gconf->comment, filter, APR_HASH_KEY_STRING, type);
	return NULL;
}



static const command_rec cmds[] = {
	AP_INIT_ITERATE2("vstatusFilter", setFilter,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Filter"),
	AP_INIT_TAKE2("vstatusFormat",setFormat,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Display format"),
	AP_INIT_TAKE1("vstatusGranularity", setGranularity,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Filter"),
	AP_INIT_TAKE1("vstatusHistSize", setHistSize,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Filter"),
	AP_INIT_TAKE2("vstatusDelta", setDelta,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Delta"),
	AP_INIT_TAKE2("vstatusType", setType,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Type"),
	AP_INIT_TAKE2("vstatusComment", setComment,
		NULL, RSRC_CONF | ACCESS_CONF,
		"Set Comment"),
	{NULL}
};

static int init(apr_pool_t * p, apr_pool_t * plog, apr_pool_t * ptemp, server_rec * s){
	ap_directive_t *dir,*dirc;
	int i,j;
	apr_status_t status;
	apr_size_t shm_size;
	apr_size_t retsize;
	apr_status_t rv;
	void *shmstart;
	apr_hash_t* hosts=apr_hash_make(p);
	apr_hash_index_t *hi;
	char* key;
	char* val;


	vstatus_cfg *cfg = ap_get_module_config(s->module_config, &vstatus_module);

	status = apr_atomic_init(p);
	if (status != APR_SUCCESS)
	        return HTTP_INTERNAL_SERVER_ERROR;

	for (dir = ap_conftree; dir; dir = dir->next) {
		for(dirc=dir->first_child;dirc;dirc=dirc->next){
			if (dirc != NULL){
				if(strcmp(dirc->directive,"ServerName")==0){
					num_vhosts++;
					apr_hash_set(hosts,dirc->args,sizeof(dirc->directive),dirc->args);
#ifdef DEBUG
					ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s,
					"mod_vstatus : VirtualHost %d ServerName %s",num_vhosts,dirc->args);
#endif
				}

			}
		}
	}

	num_vhosts=apr_hash_count(hosts);
	num_vhosts++;		//1 for ServerName
	num_vhosts++;		//1 for Aggregated Data

	shm_size = (sizeof(vstatus_ringbuffer) + num_vhosts*sizeof(vstatus_data))*gconf->histSize+3*sizeof(int);

	if (shm) {
		status = apr_shm_destroy(shm);
		if (status != APR_SUCCESS) {
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Couldn't destroy old memory block");
			return status;
		}else {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "mod_reqstatus : Old Shared memory block, destroyed.");
		}
	}

	/* Create shared memory block */
#ifdef DEBUG
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Allocating SHM (%i Bytes)",shm_size);
#endif

	status = apr_shm_create(&shm, shm_size, NULL, p);
	if(status == APR_ENOTIMPL ){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Anonymous SHM not supported");
		status = apr_shm_create(&shm, shm_size, SHM_FILENAME, p);
	}
	if (status != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Error creating shm block");
		return status;
	}
	/* Check size of shared memory block - did we get what we asked for? */
	retsize = apr_shm_size_get(shm);
	if (retsize != shm_size) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Error allocating shared memory block\n");
		return status;
	}

	/* Init shm block */
	retsize = apr_shm_size_get(shm);
	shmstart = apr_shm_baseaddr_get(shm);

	if (shmstart == NULL) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Error creating SHM block.\n");
		return status;
	}

#ifdef DEBUG
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : shm_size                 : %u",shm_size);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : retsize                  : %u",retsize);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : shm_start                : %u",shmstart);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : shm_end                  : %u",shmstart+retsize);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : sizeof vstatus_ringbuffer: %u",sizeof(vstatus_ringbuffer));
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : sizeof vstatus_data      : %u",sizeof(vstatus_data));
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : shm_size                 : %u",shm_size);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : num_vhosts               : %u",num_vhosts);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : gconf->histSize          : %u",cfg->histSize);
ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : SHM Adr %u",shmstart);

ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"mod_reqstatus : Segmenting SHM");
#endif

	bucket=shmstart;
	old_bucket=shmstart+sizeof(int);
	rbuffer = shmstart+2*sizeof(int);

	memset(shmstart, 0, retsize);

	for(i=0;i<gconf->histSize;i++){
		j=1;
		rbuffer[i].data = shmstart+2*sizeof(int) + ((cfg->histSize)*sizeof(vstatus_ringbuffer)) + i*(num_vhosts*sizeof(vstatus_data));

		for (hi = apr_hash_first(p, hosts); hi; hi = apr_hash_next(hi)) {
			j++;
			apr_hash_this(hi, &key, NULL, &val);
			rbuffer[i].data[j].hostname = apr_palloc(cfg->pool,strlen(key)+1);
			strcpy(rbuffer[i].data[j].hostname, key);
		}

		rbuffer[i].data[0].hostname = apr_palloc(cfg->pool,strlen("Total")+1);
		rbuffer[i].data[1].hostname = apr_palloc(cfg->pool,strlen("__HOST__")+1);
		strcpy(rbuffer[i].data[0].hostname,"Total");
//		strcpy(rbuffer[i].data[1].hostname,s->server_hostname);
		strcpy(rbuffer[i].data[1].hostname,"__HOST__");

	}

	ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s,"mod_vstatus : Version %s - Initialized [%d VHosts] %i Bytes allocated", VERSION,num_vhosts,(int)shm_size);
	return OK;
}

static void vstatus_hooks(apr_pool_t * p){
	// register the handler first and the log hook last so we get the complte time profile

	ap_hook_handler(handle, NULL, NULL, APR_HOOK_FIRST);		//Weiche für die Anzeige
	ap_hook_post_config(init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_log_transaction(logRequest, NULL, NULL, APR_HOOK_LAST);
}

module vstatus_module = {
	STANDARD20_MODULE_STUFF,
	NULL,			/* create per-directory configuration record <Location>, <Directory>, <Files>.*/
	NULL,			/* merge per-directory configuration records */
	init_vstatus_cfg, 	/* create per-server configuration record <VirtualHost> */
	NULL,			/* merge per-server configuration records */
	cmds, 			/* Configuration directives */
	vstatus_hooks		/* register modules functions with the core */
};
