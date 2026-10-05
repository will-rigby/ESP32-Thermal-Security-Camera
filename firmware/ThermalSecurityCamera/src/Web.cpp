#include "Application.h"
#include "Video.h"
#include "WebUi.h"
#include "ViewerUi.h"
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
  uint8_t* data=static_cast<uint8_t*>(ps_malloc(JpegCapacity)); size_t len=0; uint32_t serial=0;
  if(!data) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
  if(!copyJpeg(data,JpegCapacity,len,serial)) { free(data); return json(r,"{\"error\":\"No fresh frame\"}","503 Service Unavailable"); }
  httpd_resp_set_type(r,"image/jpeg"); httpd_resp_set_hdr(r,"Cache-Control","no-store");
  esp_err_t result=httpd_resp_send(r,reinterpret_cast<char*>(data),len); free(data); return result;
}
void streamTask(void* arg) {
  auto r=static_cast<httpd_req_t*>(arg);
  uint8_t* data=static_cast<uint8_t*>(ps_malloc(JpegCapacity));
  if(data) {
    httpd_resp_set_type(r,"multipart/x-mixed-replace; boundary=thermalframe");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    uint32_t previous=0,waitingSince=millis();
    for(;;) {
      size_t len=0; uint32_t serial=0;
      if(copyJpeg(data,JpegCapacity,len,serial) && serial!=previous) {
        char header[112]; int n=snprintf(header,sizeof(header),"--thermalframe\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",unsigned(len));
        if(httpd_resp_send_chunk(r,header,n)!=ESP_OK ||
           httpd_resp_send_chunk(r,reinterpret_cast<char*>(data),len)!=ESP_OK ||
           httpd_resp_send_chunk(r,"\r\n",2)!=ESP_OK) break;
        previous=serial; waitingSince=millis();
      } else if(uint32_t(millis()-waitingSince)>3000) break;
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    free(data);
    httpd_resp_send_chunk(r,nullptr,0);
  } else httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
  httpd_req_async_handler_complete(r); --streams; vTaskDeleteWithCaps(nullptr);
}
esp_err_t stream(httpd_req_t* r) {
  if(streams.fetch_add(1)>=2) { --streams; return json(r,"{\"error\":\"Two viewers already connected\"}","503 Service Unavailable"); }
  httpd_req_t* async=nullptr;
  if(httpd_req_async_handler_begin(r,&async)!=ESP_OK) { --streams; return ESP_FAIL; }
  // Streaming never writes NVS. Configuration handlers retain the HTTP
  // server's internal-RAM stack so Preferences writes remain safe.
  if(xTaskCreatePinnedToCoreWithCaps(streamTask,"thermal-http-stream",4096,async,1,nullptr,0,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
    --streams; httpd_resp_send_err(async,HTTPD_500_INTERNAL_SERVER_ERROR,"No task memory"); httpd_req_async_handler_complete(async);
  }
  return ESP_OK;
}
}
void startWeb() {
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.max_uri_handlers=9; cfg.max_open_sockets=7;
  cfg.stack_size=6144; cfg.recv_wait_timeout=3; cfg.send_wait_timeout=2; cfg.lru_purge_enable=true;
  httpd_handle_t server=nullptr;
  if(httpd_start(&server,&cfg)!=ESP_OK) { Serial.println("HTTP server failed"); return; }
  const httpd_uri_t routes[]={
    {"/",HTTP_GET,home,nullptr}, {"/settings",HTTP_GET,settingsPage,nullptr}, {"/api/config",HTTP_GET,getConfig,nullptr},
    {"/api/config",HTTP_PUT,putConfig,nullptr}, {"/api/status",HTTP_GET,status,nullptr},
    {"/api/relearn",HTTP_POST,relearn,nullptr}, {"/snapshot.jpg",HTTP_GET,snapshot,nullptr},
    {"/stream.mjpg",HTTP_GET,stream,nullptr}
  };
  for(auto& route:routes) if(httpd_register_uri_handler(server,&route)!=ESP_OK) Serial.println("HTTP route registration failed");
}
}
