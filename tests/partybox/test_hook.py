"""Actual C hook regression: native channel identity precedes public callbacks.

Host SDK-shaped fixture checks behavior, not Xtensa layout. Target builds retain
assertions against the real pinned SDK headers; verify_build checks relocation.
"""

import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
fixture = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BTA_AV_NUM_RCB 2
#define BTA_AV_NUM_STRS 1
#define BTA_AV_NUM_LINKS 1
#define BTA_AV_RC_CONN_MASK 0x20
typedef struct {uint8_t ctype,subunit_type,subunit_id,opcode;} tAVRC_HDR;
typedef struct {tAVRC_HDR hdr;uint32_t company_id;uint8_t *p_vendor_data;uint16_t vendor_len;} tAVRC_MSG_VENDOR;
typedef union {tAVRC_MSG_VENDOR vendor;} tAVRC_MSG;
typedef struct {tAVRC_MSG msg;uint8_t handle,label,opcode;} tBTA_AV_RC_MSG;
typedef struct {uint16_t event,layer_specific;} BT_HDR;
typedef struct {BT_HDR hdr;tAVRC_MSG_VENDOR msg;uint8_t label;} tBTA_AV_API_VENDOR;
typedef struct {uint8_t handle;} tBTA_AV_RC_CONN_CHG;
typedef union {BT_HDR hdr;tBTA_AV_RC_MSG rc_msg;tBTA_AV_API_VENDOR api_vendor;} tBTA_AV_DATA;
#define BTA_AV_API_VENDOR_CMD_EVT 42
uint16_t AVRC_VendorCmd(uint8_t,uint8_t,tAVRC_MSG_VENDOR*);
uint16_t AVRC_Close(uint8_t);
void bta_sys_sendmsg(void*);
typedef struct {uint8_t status,handle,shdl,lidx;} tBTA_AV_RCB;
typedef struct {uint8_t peer_addr[6];} tBTA_AV_SCB;
typedef struct {uint8_t addr[6];} tBTA_AV_LCB;
typedef struct {tBTA_AV_RCB rcb[2];tBTA_AV_SCB *p_scb[1];tBTA_AV_LCB lcb[2];} tBTA_AV_CB;
uint16_t AVRC_VendorRsp(uint8_t,uint8_t,tAVRC_MSG_VENDOR*);
"""
program = r"""
#include <assert.h>
#include <string.h>
#define _Static_assert(...)
#include "HOOK_SOURCE"
#undef _Static_assert
static unsigned forwarded,received,replied,sent,closed,acks,tx_status_events,last_tx_status;
static uint16_t vendor_status;
static void *queued;
static uint32_t observed_session;
static uint32_t word;
static unsigned metadata_events;
static uint32_t metadata_company;
static uint8_t metadata_ctype;
static uint16_t metadata_length;
static int16_t metadata_pdu;
static const uint8_t peer[6]={1,2,3,4,5,6};

bool partybox_peer_matches(const uint8_t *address) {return !memcmp(address,peer,6);}
void partybox_native_vendor_event(uint32_t epoch,uint8_t handle,uint8_t label,uint32_t company,uint8_t ctype,uint16_t length,int16_t pdu) {
 assert(handle==0 && label==7);++metadata_events;
 metadata_company=company;metadata_ctype=ctype;metadata_length=length;metadata_pdu=pdu;
}
void partybox_native_event(uint8_t type,uint32_t epoch,uint8_t handle,uint8_t label,uint32_t value) {
 (void)handle;(void)label;
 if(type==1){++received;word=value;observed_session=epoch;}
 if(type==2)++acks;
 if(type==3)++closed;
 if(type==10){++tx_status_events;last_tx_status=value;}
}
void bta_sys_sendmsg(void *p){queued=p;}
uint16_t AVRC_Close(uint8_t handle){assert(handle==0);return 0;}
void __real_bta_av_rc_vendor_cmd(tBTA_AV_CB *cb,tBTA_AV_DATA *data){++forwarded;}
void __real_bta_av_rc_closed(tBTA_AV_DATA *data){}
uint16_t AVRC_VendorCmd(uint8_t handle,uint8_t label,tAVRC_MSG_VENDOR *value) {
 assert(handle==0 && value->company_id==0x1958 && value->vendor_len==9);
 assert(value->p_vendor_data[5]==0 && value->p_vendor_data[6]==7 && value->p_vendor_data[8]==10);++sent;
 return vendor_status;
}
void __real_bta_av_rc_msg(tBTA_AV_CB *cb,tBTA_AV_DATA *data) {++forwarded;}
uint16_t AVRC_VendorRsp(uint8_t handle,uint8_t label,tAVRC_MSG_VENDOR *value) {
 assert(handle==0 && label==7);
 if(value->company_id==0x1958) {
  const uint8_t expected[]={0x19,0,0,0};
  assert(value->hdr.ctype==9 && value->vendor_len==4 && !memcmp(value->p_vendor_data,expected,4));
 } else {assert(value->company_id==0xC4 && value->hdr.ctype==12 && value->vendor_len==5);}
 ++replied;return 0;
}
int main(void) {
 tBTA_AV_SCB scb={};memcpy(scb.peer_addr,peer,6);
 tBTA_AV_CB cb={};cb.p_scb[0]=&scb;cb.rcb[0]=(tBTA_AV_RCB){0x20,0,1,0};
 uint8_t bytes[]={0x19,0,0,5,0xA5,9,11,0x12,0xC4};
 tBTA_AV_DATA data={};data.rc_msg.handle=0;data.rc_msg.label=7;
 tAVRC_MSG_VENDOR *v=&data.rc_msg.msg.vendor;
 *v=(tAVRC_MSG_VENDOR){{0,9,0,0},0x1958,bytes,9};
 // Public CT callback has not fired. The established native peer must suffice.
 __wrap_bta_av_rc_msg(&cb,&data);
 assert(received==1 && word==0x090B12C4 && replied==1 && !forwarded);
 // RC-only link uses the bounded, 1-based native LCB index.
 cb.rcb[0].shdl=0;cb.rcb[0].lidx=2;memcpy(cb.lcb[1].addr,peer,6);
 __wrap_bta_av_rc_msg(&cb,&data);assert(received==2 && replied==2);
 cb.lcb[1].addr[0]=9;
 __wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==1 && received==2);
 cb.lcb[1].addr[0]=1;
 data.rc_msg.handle=2;__wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==2);
 data.rc_msg.handle=0;cb.rcb[0].status=0;
 __wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==3);cb.rcb[0].status=0x20;
 cb.rcb[0].lidx=3;__wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==4);cb.rcb[0].lidx=2;
 v->hdr.ctype=9;__wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==5);v->hdr.ctype=0;
 v->vendor_len=8;__wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==6);v->vendor_len=9;
 bytes[1]=1;__wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==7);bytes[1]=0;
 v->p_vendor_data=NULL;__wrap_bta_av_rc_msg(&cb,&data);assert(forwarded==8);
 uint8_t f5[]={0xF5,0,0,1,0};
 *v=(tAVRC_MSG_VENDOR){{1,9,0,0},0xC4,f5,5};
 __wrap_bta_av_rc_msg(&cb,&data);assert(replied==3 && received==2);
 __wrap_bta_av_rc_msg(NULL,&data);__wrap_bta_av_rc_msg(&cb,NULL);assert(forwarded==10);
 uint8_t ack[]={0x19,0,0,0};
 *v=(tAVRC_MSG_VENDOR){{9,9,0,0},0x1958,ack,4};
 __wrap_bta_av_rc_msg(&cb,&data);assert(acks==1);
 v->vendor_len=3;__wrap_bta_av_rc_msg(&cb,&data);assert(acks==1);
 // Native dispatch can reject a queued command locally: AVCT_NOT_OPEN=4.
 // Surface that status rather than waiting for a peer ACK that cannot arrive.
 vendor_status=4;
 assert(partybox_queue_word(observed_session,0,7,0x0007000A));
 __wrap_bta_av_rc_vendor_cmd(&cb,queued);free(queued);
 assert(tx_status_events==1 && last_tx_status==4);
 vendor_status=0;sent=0;tx_status_events=0;
 assert(partybox_queue_word(observed_session,0,7,0x0007000A));
 __wrap_bta_av_rc_vendor_cmd(&cb,queued);assert(sent==1);
 tBTA_AV_RC_CONN_CHG close={0};__wrap_bta_av_rc_closed((tBTA_AV_DATA*)&close);assert(closed==1);
 __wrap_bta_av_rc_vendor_cmd(&cb,queued);assert(sent==1);free(queued);
 // Reopen same native handle: previous queued session must still be rejected.
 uint32_t old_session=observed_session;
 *v=(tAVRC_MSG_VENDOR){{0,9,0,0},0x1958,bytes,9};
 __wrap_bta_av_rc_msg(&cb,&data);assert(observed_session!=old_session);
 assert(partybox_queue_word(old_session,0,7,0x0007000A));
 __wrap_bta_av_rc_vendor_cmd(&cb,queued);assert(sent==1);free(queued);
 assert(partybox_queue_word(observed_session,0,7,0x0007000A));
 cb.rcb[0].status=0;__wrap_bta_av_rc_vendor_cmd(&cb,queued);assert(sent==1);free(queued);
 // Selected-peer unhandled vendor commands still go to the stock handler,
 // with bounded metadata only; GetCapabilities receives no new private reply.
 cb.rcb[0].status=0x20;
 uint8_t capabilities[]={0x10,0,0,1,3};
 *v=(tAVRC_MSG_VENDOR){{1,9,0,0},0x1958,capabilities,sizeof(capabilities)};
 unsigned before_forwarded=forwarded,before_replied=replied,before_metadata=metadata_events;
 __wrap_bta_av_rc_msg(&cb,&data);
 assert(metadata_events==before_metadata+1 && metadata_company==0x1958 && metadata_ctype==1 && metadata_pdu==0x10 && metadata_length==5);
 assert(forwarded==before_forwarded+1 && replied==before_replied);
 cb.lcb[1].addr[0]=9;__wrap_bta_av_rc_msg(&cb,&data);
 assert(metadata_events==before_metadata+1 && forwarded==before_forwarded+2);cb.lcb[1].addr[0]=1;
 v->vendor_len=0;__wrap_bta_av_rc_msg(&cb,&data);
 assert(metadata_length==0 && metadata_pdu==-1);
 v->vendor_len=UINT16_MAX;v->p_vendor_data=NULL;__wrap_bta_av_rc_msg(&cb,&data);
 assert(metadata_length==UINT16_MAX && metadata_pdu==-1);
 v->p_vendor_data=capabilities;__wrap_bta_av_rc_msg(&cb,&data);
 assert(metadata_length==UINT16_MAX && metadata_pdu==0x10);
 assert(forwarded==before_forwarded+5 && replied==before_replied);
}
"""
with tempfile.TemporaryDirectory(prefix="partybox-esphome-hook-") as folder:
    temporary = Path(folder)
    (temporary / "stack").mkdir()
    (temporary / "osi").mkdir()
    (temporary / "bta").mkdir()
    (temporary / "osi/allocator.h").write_text(
        "#include <stdlib.h>\n#define osi_calloc(n) calloc(1,n)\n"
    )
    (temporary / "bta/bta_sys.h").write_text('#include "bta_av_int.h"\n')
    (temporary / "bta_av_int.h").write_text(fixture)
    (temporary / "stack/avrc_api.h").write_text('#include "bta_av_int.h"\n')
    source = temporary / "test.c"
    hook = root / "components/partybox/partybox_bluedroid/tws_hook.c"
    source.write_text(program.replace("HOOK_SOURCE", str(hook)))
    binary = temporary / "test"
    subprocess.run(
        [
            shutil.which("clang") or "cc",
            "-std=c11",
            "-I",
            str(temporary),
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print(
    "PASS: production C hook, early RX, strict ACK, queued TX, stale-session and native close guards"
)
