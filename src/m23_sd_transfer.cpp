// Temporary qualification firmware only. Single filesystem owner, no audio/UI.
#include <Arduino.h>
#include "hal/storage_hal.h"
#include "mbedtls/sha256.h"
#include <cctype>
namespace {
File output;
char final_path[160]{},partial_path[176]{},expected[65]{};
uint32_t expected_size=0;
bool hash_file(const char *path,char *hex) {
  File f=storage::open(path);if(!f || f.isDirectory())return false;
  mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);mbedtls_sha256_starts(&ctx,0);
  uint8_t buffer[512],digest[32];size_t remaining=f.size();
  while(remaining) { size_t n=std::min(remaining,sizeof(buffer));if(f.read(buffer,n)!=n){mbedtls_sha256_free(&ctx);return false;}mbedtls_sha256_update(&ctx,buffer,n);remaining-=n;yield(); }
  mbedtls_sha256_finish(&ctx,digest);mbedtls_sha256_free(&ctx);f.close();
  for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);return true;
}
bool valid_name(const char *name) {
  if(strncmp(name,"M23_",4) || strlen(name)>95)return false;
  for(const char *p=name;*p;++p)if(!isalnum(*p) && *p!='_' && *p!='.')return false;
  return !strstr(name,"..") && (String(name).endsWith(".wav") || String(name).endsWith(".json") || String(name).endsWith(".txt"));
}
int nibble(char c) { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1; }
void command(char *line) {
  if(!strcmp(line,"PING")){Serial.println("M23 READY");return;}
  if(!strcmp(line,"INVENTORY")) {
    File dir=storage::open("/P4SDM/SAMPLES");unsigned wavs=0;
    while(dir) { File f=dir.openNextFile();if(!f)break;if(!f.isDirectory() && String(f.name()).endsWith(".wav"))++wavs;Serial.printf("M23 FILE %s %u\n",f.name(),unsigned(f.size())); }
    Serial.printf("M23 COUNT %u\n",wavs);return;
  }
  if(!strncmp(line,"BEGIN ",6)) {
    output.close();char name[96],hash[65],extra;unsigned size;
    if(sscanf(line+6,"%95s %u %64s %c",name,&size,hash,&extra)!=3 || !valid_name(name) || !size || size>4194304 || strlen(hash)!=64){Serial.println("M23 ERROR begin");return;}
    for(unsigned i=0;i<64;++i)if(nibble(hash[i])<0){Serial.println("M23 ERROR hash");return;}
    snprintf(final_path,sizeof(final_path),"/P4SDM/%s/%s",String(name).endsWith(".wav")?"SAMPLES":"M23",name);
    snprintf(partial_path,sizeof(partial_path),"%s.m23part",final_path);
    char actual[65];
    if(SD_MMC.exists(final_path)) {
      if(hash_file(final_path,actual) && !strcmp(hash,actual))Serial.printf("M23 EXISTS %s\n",actual);
      else Serial.println("M23 ERROR collision");return;
    }
    expected_size=size;strcpy(expected,hash);
    if(SD_MMC.exists(partial_path)) {
      File p=storage::open(partial_path);unsigned old=p.size();p.close();
      if(old>size || old%256){SD_MMC.remove(partial_path);}
    }
    output=SD_MMC.open(partial_path,FILE_APPEND);
    if(!output){Serial.println("M23 ERROR open");return;}
    Serial.printf("M23 OFFSET %u\n",unsigned(output.size()));return;
  }
  if(!strncmp(line,"DATA ",5)) {
    unsigned offset,checksum;char hex[513],extra;
    if(!output || sscanf(line+5,"%u %u %512s %c",&offset,&checksum,hex,&extra)!=3 || offset!=output.size()){Serial.println("M23 ERROR offset");return;}
    const unsigned chars=strlen(hex);uint8_t data[256];unsigned a=1,b=0;
    if(!chars || chars%2 || offset+chars/2>expected_size){Serial.println("M23 ERROR size");return;}
    for(unsigned i=0;i<chars/2;++i) {int hi=nibble(hex[2*i]),lo=nibble(hex[2*i+1]);if(hi<0||lo<0){Serial.println("M23 ERROR hex");return;}data[i]=(hi<<4)|lo;a=(a+data[i])%65521;b=(b+a)%65521;}
    if(((b<<16)|a)!=checksum){Serial.println("M23 ERROR checksum");return;}
    if(output.write(data,chars/2)!=chars/2){output.close();Serial.println("M23 ERROR write");return;}
    output.flush();Serial.printf("M23 ACK %u\n",offset+chars/2);return;
  }
  if(!strcmp(line,"END")) {
    if(!output || output.size()!=expected_size){Serial.println("M23 ERROR incomplete");return;}
    output.flush();output.close();char actual[65];
    if(!hash_file(partial_path,actual) || strcmp(actual,expected)){SD_MMC.remove(partial_path);Serial.println("M23 ERROR full_hash");return;}
    if(SD_MMC.exists(final_path) || !SD_MMC.rename(partial_path,final_path)){Serial.println("M23 ERROR rename");return;}
    if(!hash_file(final_path,actual) || strcmp(actual,expected)){Serial.println("M23 ERROR reopen");return;}
    Serial.printf("M23 VERIFIED %s\n",actual);return;
  }
  Serial.println("M23 ERROR command");
}
}
void setup(){Serial.setRxBufferSize(2048);Serial.begin(115200);delay(1500);if(!storage::mount())return;SD_MMC.mkdir("/P4SDM");SD_MMC.mkdir("/P4SDM/SAMPLES");SD_MMC.mkdir("/P4SDM/M23");Serial.println("M23 READY");}
void loop(){static char line[640];static unsigned used=0;static bool discard=false;
  while(Serial.available()){char c=Serial.read();if(c=='\r')continue;if(c=='\n'){if(discard)Serial.println("M23 ERROR line");else{line[used]=0;command(line);}used=0;discard=false;}else if(used+1<sizeof(line)&&!discard)line[used++]=c;else discard=true;}delay(1);}
