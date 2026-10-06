#include "Application.h"
#include "Video.h"
#include "WebUi.h"
#include "ViewerUi.h"
#include "VideoClientUi.h"
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
namespace thermal {
namespace {
std::atomic<unsigned> streams{0};
esp_err_t json(httpd_req_t* request,const String& value,const char* status="200 OK") {
  httpd_resp_set_status(request,status); httpd_resp_set_type(request,"application/json");
  httpd_resp_set_hdr(request,"Cache-Control","no-store");
  return httpd_resp_send(request,value.c_str(),value.length());
}
esp_err_t home(httpd_req_t* r) {
  httpd_resp_set_type(r,"text/html; charset=utf-8");
  httpd_resp_set_hdr(r,"Cache-Control","no-store");
  return httpd_resp_send(r,ViewerUi,HTTPD_RESP_USE_STRLEN);
}
esp_err_t settingsPage(httpd_req_t* r) {
  httpd_resp_set_type(r,"text/html; charset=utf-8");
  httpd_resp_set_hdr(r,"Cache-Control","no-store");
  return httpd_resp_send(r,WebUi,HTTPD_RESP_USE_STRLEN);
}
esp_err_t getConfig(httpd_req_t* r) { return json(r,configStore.json()); }
esp_err_t status(httpd_req_t* r) { return json(r,statusJson()); }
bool writable(httpd_req_t* r) {
  char header[40]{};
  return httpd_req_get_hdr_value_str(r,"X-Thermal-Request",header,sizeof(header))==ESP_OK && !strcmp(header,"1");
}
esp_err_t putConfig(httpd_req_t* r) {
  if(!writable(r)) return json(r,"{\"error\":\"X-Thermal-Request: 1 required\"}","403 Forbidden");
  if(r->content_len<2 || r->content_len>4096) return json(r,"{\"error\":\"Body must be 2-4096 bytes\"}","400 Bad Request");
  char* body=static_cast<char*>(malloc(r->content_len+1));
  if(!body) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
  size_t read=0;
  while(read<r->content_len) {
    int n=httpd_req_recv(r,body+read,r->content_len-read);
    if(n<=0) { free(body); return httpd_resp_send_err(r,HTTPD_408_REQ_TIMEOUT,"Incomplete request"); }
    read+=n;
  }
  body[read]=0; String error;
  const bool ok=configStore.update(body,error); free(body);
  if(ok) return json(r,"{\"saved\":true,\"background_learning_restarted\":true}");
  cJSON* e=cJSON_CreateObject(); cJSON_AddStringToObject(e,"error",error.c_str());
  char* text=cJSON_PrintUnformatted(e); String result=text?text:"{}"; cJSON_free(text); cJSON_Delete(e);
  return json(r,result,"400 Bad Request");
}
esp_err_t relearn(httpd_req_t* r) {
  if(!writable(r)) return json(r,"{\"error\":\"X-Thermal-Request: 1 required\"}","403 Forbidden");
  ++relearnGeneration; return json(r,"{\"learning\":true}");
}
esp_err_t snapshot(httpd_req_t* r) {
  uint8_t* memory=static_cast<uint8_t*>(ps_malloc(VideoPacketBytes+BmpBytes));
  if(!memory)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
  if(!copyVideo(memory,VideoPacketBytes)) { free(memory);return json(r,"{\"error\":\"No fresh frame\"}","503 Service Unavailable"); }
  auto bmp=memory+VideoPacketBytes;renderBmp(memory,bmp);
  httpd_resp_set_type(r,"image/bmp");httpd_resp_set_hdr(r,"Cache-Control","no-store");
  const auto result=httpd_resp_send(r,reinterpret_cast<char*>(bmp),BmpBytes);free(memory);return result;
}
esp_err_t videoScript(httpd_req_t* r) {
  httpd_resp_set_type(r,"application/javascript");httpd_resp_set_hdr(r,"Cache-Control","no-store");
  return httpd_resp_send(r,VideoClientUi,HTTPD_RESP_USE_STRLEN);
}
struct StreamSession { uint32_t serial;uint8_t packet[VideoPacketBytes]; };
void freeStream(void* context) { free(context);--streams; }
esp_err_t stream(httpd_req_t* r) {
  // IDF 5.5.5 handles the upgrade without invoking this handler. A GET here
  // is an ordinary HTTP request; create stream state on the first valid pull.
  if(r->method==HTTP_GET)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"WebSocket upgrade required");
  httpd_ws_frame_t request{};
  auto result=httpd_ws_recv_frame(r,&request,0);
  if(result!=ESP_OK)return result;
  // Pull protocol: exactly one binary byte (1) asks for the latest frame.
  // No history or outbound frame queue is kept for a slow viewer.
  if(request.type!=HTTPD_WS_TYPE_BINARY || request.len!=1 || !request.final)return ESP_FAIL;
  uint8_t command=0;request.payload=&command;
  result=httpd_ws_recv_frame(r,&request,1);
  if(result!=ESP_OK || command!=1)return ESP_FAIL;
  auto session=static_cast<StreamSession*>(r->sess_ctx);
  if(!session) {
    // An error closes an excess client. Session cleanup releases the slot.
    if(streams.fetch_add(1)>=2) { --streams;return ESP_FAIL; }
    session=static_cast<StreamSession*>(ps_malloc(sizeof(StreamSession)));
    if(!session) { --streams;return ESP_ERR_NO_MEM; }
    session->serial=0;r->sess_ctx=session;r->free_ctx=freeStream;
  }
  httpd_ws_frame_t response{};response.type=HTTPD_WS_TYPE_BINARY;
  if(copyVideo(session->packet,VideoPacketBytes,session->serial)) {
    response.payload=session->packet;response.len=VideoPacketBytes;
    session->serial=get32(session->packet+12);
  }
  // Empty response means there is no new healthy frame; retry after 50 ms.
  return httpd_ws_send_frame(r,&response);
}
}
void startWeb() {
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.max_uri_handlers=10;cfg.max_open_sockets=7;
  cfg.stack_size=6144;cfg.recv_wait_timeout=3;cfg.send_wait_timeout=1;cfg.lru_purge_enable=true;
  httpd_handle_t server=nullptr;
  if(httpd_start(&server,&cfg)!=ESP_OK) { Serial.println("HTTP server failed");return; }
  const httpd_uri_t routes[]={
    {"/",HTTP_GET,home,nullptr},{"/settings",HTTP_GET,settingsPage,nullptr},
    {"/api/config",HTTP_GET,getConfig,nullptr},{"/api/config",HTTP_PUT,putConfig,nullptr},
    {"/api/status",HTTP_GET,status,nullptr},{"/api/relearn",HTTP_POST,relearn,nullptr},
    {"/snapshot.bmp",HTTP_GET,snapshot,nullptr},{"/video.js",HTTP_GET,videoScript,nullptr}
  };
  for(auto& route:routes)if(httpd_register_uri_handler(server,&route)!=ESP_OK)Serial.println("HTTP route registration failed");
  httpd_uri_t ws{};ws.uri="/stream.yuy2";ws.method=HTTP_GET;ws.handler=stream;ws.is_websocket=true;
  if(httpd_register_uri_handler(server,&ws)!=ESP_OK)Serial.println("WebSocket route registration failed");
}
}
