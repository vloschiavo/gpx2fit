#define _POSIX_C_SOURCE 200809L
#include "course.h"
#include "decimate.h"
#include "diag.h"
#include "fit_write.h"
#include "geo.h"
#include "gpx.h"
#include "names.h"
#include "snap.h"
#include "sport.h"
#include "symmap.h"
#include "timing.h"
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

struct gpx2fit_result {
    int      rc;
    uint8_t *fit_data;
    size_t   fit_len;
    char    *json;
};

struct jbuf { char *data; size_t len, cap; int failed; };

static void jbuf_init(struct jbuf *b){ b->data=NULL; b->len=0; b->cap=0; b->failed=0; }
static void jbuf_free(struct jbuf *b){ free(b->data); b->data=NULL; b->len=b->cap=0; }
static int jbuf_reserve(struct jbuf *b, size_t extra){
    if(b->failed) return -1;
    if(b->len+extra+1<=b->cap) return 0;
    size_t ncap=b->cap?b->cap*2:256;
    while(ncap<b->len+extra+1) ncap*=2;
    char *nd=realloc(b->data,ncap);
    if(!nd){ b->failed=1; return -1; }
    b->data=nd; b->cap=ncap; return 0;
}
static void jbuf_append(struct jbuf *b,const char*s,size_t n){ if(jbuf_reserve(b,n)!=0) return; memcpy(b->data+b->len,s,n); b->len+=n; b->data[b->len]='\0'; }
static void jbuf_puts(struct jbuf*b,const char*s){ jbuf_append(b,s,strlen(s)); }
static void jbuf_printf(struct jbuf*b,const char*fmt,...){
    va_list ap,ap2; va_start(ap,fmt); va_copy(ap2,ap);
    int n=vsnprintf(NULL,0,fmt,ap2); va_end(ap2);
    if(n<0){ b->failed=1; va_end(ap); return; }
    if(jbuf_reserve(b,(size_t)n)!=0){ va_end(ap); return; }
    vsnprintf(b->data+b->len,(size_t)n+1,fmt,ap); b->len+=(size_t)n; va_end(ap);
}
static void jbuf_json_string(struct jbuf*b,const char*s){
    jbuf_puts(b,"\"");
    if(s){ for(const unsigned char*p=(const unsigned char*)s;*p;p++){
        unsigned char c=*p;
        switch(c){
        case '"': jbuf_puts(b,"\\\""); break;
        case '\\': jbuf_puts(b,"\\\\"); break;
        case '\n': jbuf_puts(b,"\\n"); break;
        case '\r': jbuf_puts(b,"\\r"); break;
        case '\t': jbuf_puts(b,"\\t"); break;
        default:
            if(c<0x20){ char buf[8]; snprintf(buf,sizeof buf,"\\u%04x",c); jbuf_puts(b,buf); }
            else { char ch[2]={(char)c,'\0'}; jbuf_puts(b,ch); }
            break;
        }
    }}
    jbuf_puts(b,"\"");
}

static void wasm_map_warn(const char*source,int line,const char*msg,void*ctx){
    struct diag_list*d=ctx;
    if(d) diag_list_add(d,"warning: %s:%d: %s\n",source,line,msg);
}

EMSCRIPTEN_KEEPALIVE struct gpx2fit_result *gpx2fit_convert(
    const char*gpx_xml,const char*map_text,const char*course_name,
    uint8_t sport,uint8_t sub_sport,double snap_m,const char*pass_mode,
    double pace_mps,uint32_t start_fit,long max_points,int strict)
{
    struct gpx2fit_result *res=calloc(1,sizeof*res);
    if(!res) return NULL;
    struct course c; struct symmap map; struct snap_report sr; struct ele_stats es;
    size_t n_full=0; double total_m=0.0; int trc=0;
    struct gpx_options go; struct diag_list diag; struct jbuf jb;
    char err[256]; char errmsg[256];
    course_init(&c); symmap_init(&map); diag_list_init(&diag); jbuf_init(&jb);
    memset(&sr,0,sizeof sr); memset(&es,0,sizeof es);
    err[0]='\0'; errmsg[0]='\0';
    int ok=1;

    if(!gpx_xml||gpx_xml[0]=='\0'){ snprintf(errmsg,sizeof errmsg,"missing GPX input"); ok=0; }
    if(ok&&(!isfinite(snap_m)||snap_m<0.0)){ snprintf(errmsg,sizeof errmsg,"invalid snap distance"); ok=0; }
    if(ok&&(!isfinite(pace_mps)||pace_mps<=0.0)){ snprintf(errmsg,sizeof errmsg,"invalid pace"); ok=0; }
    if(ok&&(start_fit<FIT_DATE_MIN||start_fit>FIT_TIME_MAX)){ snprintf(errmsg,sizeof errmsg,"invalid start time"); ok=0; }
    if(ok&&max_points!=0&&max_points<2){ snprintf(errmsg,sizeof errmsg,"invalid max-points"); ok=0; }
    enum snap_mode smode=SNAP_NEAREST;
    if(ok&&pass_mode&&pass_mode[0]){ if(snap_mode_parse(pass_mode,&smode)!=0){ snprintf(errmsg,sizeof errmsg,"invalid pass mode"); ok=0; } }

    if(ok){
        if(map_text&&map_text[0]){
            int rc=symmap_parse(map_text,"custom.map",&map,wasm_map_warn,&diag);
            if(rc!=SYMMAP_OK){ snprintf(errmsg,sizeof errmsg,"out of memory"); ok=0; }
        }
        if(ok){
            int rc=symmap_load_defaults(&map,wasm_map_warn,&diag);
            if(rc!=SYMMAP_OK){ snprintf(errmsg,sizeof errmsg,"out of memory"); ok=0; }
        }
    }
    if(ok){
        go.track=0; go.quiet=true; go.diag=&diag;
        size_t glen=strlen(gpx_xml);
        int rc=read_gpx_mem(gpx_xml,glen,NULL,&c,&go,err,sizeof err);
        if(rc!=0){ snprintf(errmsg,sizeof errmsg,"%s",err[0]?err:"failed to read GPX"); ok=0; }
    }
    if(ok&&course_name&&course_name[0]){
        char*n=strdup(course_name);
        if(!n){ snprintf(errmsg,sizeof errmsg,"out of memory"); ok=0; }
        else { free(c.name); c.name=n; }
    }
    if(ok&&c.name&&strlen(c.name)>COURSE_NAME_MAX){
        diag_list_add(&diag,"warning: course name is longer than %d bytes, truncating\n",COURSE_NAME_MAX);
        utf8_truncate(c.name,strlen(c.name)+1,c.name,COURSE_NAME_MAX);
    }
    if(ok) course_compute_distances(&c);
    if(ok){
        struct snap_options so={smode,snap_m,true,(bool)strict,&diag};
        int src=snap_waypoints(&c,&so,&sr);
        if(src==SNAP_NOMEM){ snprintf(errmsg,sizeof errmsg,"out of memory"); ok=0; }
        else if(src==SNAP_STRICT){ snprintf(errmsg,sizeof errmsg,"--strict: %zu waypoint(s) farther than %g m from the track",sr.n_skipped,snap_m); ok=0; }
    }
    if(ok) course_apply_types(&c,&map,true,&diag);
    if(ok){
        trc=course_assign_times(&c,start_fit,pace_mps);
        if(trc!=TIMING_OK){ snprintf(errmsg,sizeof errmsg,"%s",timing_strerror(trc)); ok=0; }
    }
    if(ok) course_assign_cp_times(&c);
    if(ok){ n_full=c.n_tp; total_m=c.n_tp?c.tp[c.n_tp-1].dist:0.0; course_elevation_stats(&c,1.0,&es); }
    if(ok&&max_points>0){
        struct decimate_report dr;
        if(course_decimate(&c,(size_t)max_points,&dr)!=0){ snprintf(errmsg,sizeof errmsg,"out of memory"); ok=0; }
        else if(dr.exceeded){
            if(strict){ snprintf(errmsg,sizeof errmsg,"--strict: --max-points %ld is less than the %zu points that must be kept",max_points,dr.n_required); ok=0; }
            else diag_list_add(&diag,"warning: --max-points %ld is less than the %zu points that must be kept (first, last, and one per course point); keeping %zu\n",max_points,dr.n_required,dr.n_after);
        }
    }
    struct fit_buf b; fit_buf_init(&b);
    if(ok){
        struct fit_options fo; fit_options_default(&fo); fo.sport=sport; fo.sub_sport=sub_sport;
        int frc=fit_encode_course(&c,&fo,&b);
        if(frc!=FIT_OK){ snprintf(errmsg,sizeof errmsg,"%s",fit_strerror(frc)); ok=0; }
    }
    if(ok){ res->rc=0; res->fit_data=b.data; res->fit_len=b.len; b.data=NULL; b.len=0; b.cap=0; }
    else { res->rc=1; res->fit_data=NULL; res->fit_len=0; fit_buf_free(&b); }

    jbuf_puts(&jb,"{");
    jbuf_printf(&jb,"\"ok\":%s,",ok?"true":"false");
    jbuf_puts(&jb,"\"error\":");
    if(ok) jbuf_puts(&jb,"null"); else jbuf_json_string(&jb,errmsg);
    jbuf_puts(&jb,",");
    jbuf_printf(&jb,"\"distance_m\":%.6g,",ok?total_m:0.0);
    jbuf_printf(&jb,"\"ascent_m\":%.6g,",(ok&&es.valid)?es.ascent:0.0);
    jbuf_printf(&jb,"\"descent_m\":%.6g,",(ok&&es.valid)?es.descent:0.0);
    jbuf_printf(&jb,"\"has_elevation\":%s,",(ok&&es.valid)?"true":"false");
    jbuf_printf(&jb,"\"track_points\":%zu,",ok?c.n_tp:0);
    jbuf_printf(&jb,"\"course_points\":%zu,",ok?c.n_cp:0);
    jbuf_printf(&jb,"\"waypoints_total\":%zu,",c.n_wp);
    jbuf_printf(&jb,"\"waypoints_snapped\":%zu,",ok?sr.n_snapped:0);
    jbuf_printf(&jb,"\"waypoints_skipped\":%zu,",ok?sr.n_skipped:0);
    jbuf_puts(&jb,"\"course_name\":");
    jbuf_json_string(&jb,ok?c.name:course_name);
    jbuf_puts(&jb,",");
    jbuf_printf(&jb,"\"sport\":%u,",sport);
    jbuf_printf(&jb,"\"sub_sport\":%u,",sub_sport);
    jbuf_puts(&jb,"\"course_points_list\":[");
    if(ok){
        for(size_t i=0;i<c.n_cp;i++){
            if(i) jbuf_puts(&jb,",");
            jbuf_puts(&jb,"{");
            jbuf_puts(&jb,"\"name\":"); jbuf_json_string(&jb,c.cp[i].name);
            jbuf_printf(&jb,",\"lat\":%.10g",c.cp[i].lat);
            jbuf_printf(&jb,",\"lon\":%.10g",c.cp[i].lon);
            jbuf_printf(&jb,",\"km\":%.6g",c.cp[i].dist/1000.0);
            jbuf_printf(&jb,",\"offset_m\":%.6g",c.cp[i].offset);
            jbuf_printf(&jb,",\"type\":%u}",c.cp[i].type);
        }
    }
    jbuf_puts(&jb,"],");
    jbuf_puts(&jb,"\"skipped_waypoints\":[");
    {
        int first=1;
        for(size_t i=0;i<diag.n;i++){
            const char*s=diag.items[i];
            if(!strstr(s,"skipped")) continue;
            const char*q=strstr(s,"waypoint '");
            if(!q) continue;
            q+=strlen("waypoint '");
            const char*end=strchr(q,'\'');
            if(!end) continue;
            size_t nlen=(size_t)(end-q);
            char namebuf[256];
            if(nlen>=sizeof namebuf) nlen=sizeof namebuf-1;
            memcpy(namebuf,q,nlen); namebuf[nlen]='\0';
            if(!first) jbuf_puts(&jb,",");
            first=0;
            jbuf_json_string(&jb,namebuf);
        }
    }
    jbuf_puts(&jb,"],");
    jbuf_puts(&jb,"\"warnings\":[");
    for(size_t i=0;i<diag.n;i++){ if(i) jbuf_puts(&jb,","); jbuf_json_string(&jb,diag.items[i]); }
    jbuf_puts(&jb,"]}");

    if(!jb.failed){ res->json=jb.data; jb.data=NULL; }
    else { jbuf_free(&jb); res->json=strdup("{\"ok\":false,\"error\":\"json build failed\"}"); }

    jbuf_free(&jb);
    diag_list_free(&diag);
    symmap_free(&map);
    course_free(&c);
    (void)n_full; (void)trc; (void)err;
    return res;
}

EMSCRIPTEN_KEEPALIVE uint8_t *gpx2fit_result_data(void*ptr){ struct gpx2fit_result*r=ptr; return r?r->fit_data:NULL; }
EMSCRIPTEN_KEEPALIVE size_t gpx2fit_result_len(void*ptr){ struct gpx2fit_result*r=ptr; return r?r->fit_len:0; }
EMSCRIPTEN_KEEPALIVE const char *gpx2fit_result_json(void*ptr){ struct gpx2fit_result*r=ptr; return r?r->json:NULL; }
EMSCRIPTEN_KEEPALIVE int gpx2fit_result_rc(void*ptr){ struct gpx2fit_result*r=ptr; return r?r->rc:-1; }
EMSCRIPTEN_KEEPALIVE void gpx2fit_free(void*ptr){
    struct gpx2fit_result*r=ptr;
    if(!r) return;
    free(r->fit_data); free(r->json); free(r);
}
