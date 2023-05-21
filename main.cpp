#include "mongoose.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <string>
#include <cstring>
#include <chrono>

using namespace std;

struct thread_data{
    struct mg_queue queue;
    struct mg_str body;
};

mutex mtx;
condition_variable con;
string share_msg;

static void* preThread(void* param){
    struct thread_data* d=(struct thread_data* ) param;
    char buf[100];

    mg_queue_init(&d->queue, buf, sizeof(buf)); 
    cout<<"pre: "<<d->body.ptr<<endl;
    unique_lock<mutex> lck(mtx);
    //con.wait(lck,[]()->bool{return share_msg.length()>0;});
    con.wait_for(lck,chrono::milliseconds(500),[]()->bool{return share_msg.length()>0;});
    if(share_msg.length()>0){
        mg_queue_printf(&d->queue, share_msg.c_str()); 
        share_msg.clear();
    }
    else{
        mg_queue_printf(&d->queue, "time out!");
    }
    
    // Wait until connection reads our message, then it is safe to quit
    while (d->queue.tail != d->queue.head) usleep(1000);    //待改进
    MG_INFO(("done, cleaning up..."));
    free(d);
    return NULL;
}

static void* backThread(void* param){
    struct thread_data* d=(struct thread_data* ) param;
    char buf[100];

    mg_queue_init(&d->queue, buf, sizeof(buf)); 
    cout<<"back: "<<d->body.ptr<<endl;

    share_msg=d->body.ptr;
    con.notify_one();
    mg_queue_printf(&d->queue, "pre trans to back successfully!"); 

    // Wait until connection reads our message, then it is safe to quit
    while (d->queue.tail != d->queue.head) usleep(1000);    //待改进
    MG_INFO(("done, cleaning up..."));
    free(d);
    return NULL;
}


static void fn(struct mg_connection* c, int ev, void* ev_data, void* fn_data){
    if(ev == MG_EV_HTTP_MSG){
        struct mg_http_message* hm=(struct mg_http_message*)ev_data;
        struct thread_data *d = (struct thread_data *) calloc(1, sizeof(*d));
        d->body=mg_strdup(hm->body);
        if(mg_http_match_uri(hm, "/pre")){
            thread th{preThread, d};
            th.detach();
            *(void**)c->data=d;
        }
        else if(mg_http_match_uri(hm, "/back")){
            thread th{backThread, d};
            th.detach();
            *(void**)c->data=d;
        }
    }
    else if(ev==MG_EV_POLL){
        struct thread_data*d=*(struct thread_data**)c->data;
        size_t len;
        char* buf;
        if(d!=NULL &&(len=mg_queue_next(&d->queue, &buf))>0){
            mg_http_reply(c, 200, "", "%.*s\n", (int) len, buf);
            mg_queue_del(&d->queue, len);
            *(void**) c->data=NULL;
        }
    }
}


int main(void){
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_log_set(MG_LL_DEBUG);
    mg_http_listen(&mgr, "http://0.0.0.0:8000", fn, NULL);
    while(true){
        mg_mgr_poll(&mgr, 100);
    }
    mg_mgr_free(&mgr);
    return 0;
}