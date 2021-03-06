#include "../communication/msg.h"
#include "../log/log.h"
#include "hash.h"
#include "router.h"
#include "delay.h"

static hash_table *table;

static inline uint64_t get_key(uint32_t ip,uint16_t port){
	uint64_t value=((uint64_t)ip<<16);
	value+=port;
	return value;
}

void router_init(){
	/* we support 256k slots here */
	table = hash_create(1024*256);
	strcpy(table->name,"client--src table");
	logInfo(LOG_NOTICE,"create table %s,size:%u",
			table->name,table->size);
}

static void route_table_delete_obsolete(uint64_t key)
{       
	linklist *l = get_linklist(table,key);
	hash_node *hnode=NULL;
	time_t  nowtime = time(NULL);
	while(1){
		lnodeptr node = linklist_tail(l); 
		if(! node ){
			break;
		}       
		hnode = (hash_node *)node->data;
		if(hnode->access_time+table->timeout < nowtime){
			lnodeptr tail=linklist_pop_tail(l);
			hash_node *hnode = (hash_node *)tail->data;
			if(NULL!=hnode)
			{   
				free(hnode);
			}   
			tail->data=NULL;
			free(tail);
		}else{
			break;
		}   
	} 
}

void router_del(uint32_t ip,uint16_t port){
	uint64_t key=get_key(ip,port);
	hash_del(table,key);
	delay_table_del(key);
}

void router_add(uint32_t ip,uint16_t port,int fd){
	uint64_t key=get_key(ip,port);
	hash_add(table,key,(void *)(long)fd);
	delay_table_send(key,fd);
}

void router_update(struct iphdr *ip_header){
	uint32_t size_ip;
	struct tcphdr *tcp_header=NULL;
	uint64_t key;
	void *fd=NULL;
	struct receiver_msg_st msg;
#if (TCPCOPY_MYSQL_ADVANCED) 
	unsigned char* payload=NULL;
	uint32_t size_tcp;
	uint32_t contentLen;
	uint32_t totalLen;
#endif

	if(ip_header->protocol != IPPROTO_TCP){
		logInfo(LOG_INFO,"this is not tcp packet");
		return;
	}
	size_ip = ip_header->ihl<<2;
	tcp_header = (struct tcphdr*)((char *)ip_header+size_ip);

	memset(&msg,0,sizeof(struct receiver_msg_st));
	memcpy((void *) &(msg.ip_header),ip_header,sizeof(struct iphdr));
	memcpy((void *) &(msg.tcp_header),tcp_header,sizeof(struct tcphdr));

#if (TCPCOPY_MYSQL_ADVANCED) 
	totalLen = ntohs(ip_header->tot_len);
	size_tcp = tcp_header->doff<<2;
	contentLen=totalLen-size_ip-size_tcp;
	if(contentLen>0)
	{
		payload=(unsigned char*)((char*)tcp_header+size_tcp);
		if(contentLen<=MAX_PAYLOAD_LEN)
		{
			/*
			 * only transfer payload if content length is less
			 * than MAX_PAYLOAD_LEN
			 */
			memcpy((void *) &(msg.payload),payload,contentLen);
		}
	}
#endif
	key=get_key(ip_header->daddr,tcp_header->dest);
	route_table_delete_obsolete(key);
	fd = hash_find(table,key);
	if( NULL == fd ){
		logInfo(LOG_INFO,"fd is null");
		delay_table_add(key,&msg);
		return ;
	}
	msg_receiver_send((int)(long)fd,&msg);

}

void router_destroy()
{
	if(table!=NULL)
	{
		logInfo(LOG_NOTICE,"destroy router table");
		hash_destory(table);
		free(table);
		table=NULL;
	}
}

