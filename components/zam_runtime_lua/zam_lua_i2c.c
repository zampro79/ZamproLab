#include <stdint.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "lauxlib.h"
#include "zam_lua_internal.h"

#define I2C_TRANSFER_LIMIT 256
#define I2C_TRANSFER_TIMEOUT_MS 100
#define I2C_PROBE_TIMEOUT_MS 20
static zam_lua_context_t *context(lua_State *L){return *(zam_lua_context_t **)lua_getextraspace(L);}
static int fail(lua_State *L,esp_err_t e){
    if(e==ESP_ERR_TIMEOUT)return luaL_error(L,"Timeout I2C");
    if(e==ESP_ERR_NOT_FOUND)return luaL_error(L,"Dispositivo I2C non risponde");
    if(e==ESP_FAIL)return luaL_error(L,"Errore I2C o dispositivo non risponde");
    return luaL_error(L,"Errore driver I2C: %s",esp_err_to_name(e));
}
static void count(lua_State *L,int n){if(lua_gettop(L)!=n)luaL_error(L,"numero argomenti I2C non valido");}
static lua_Integer integer(lua_State *L,int n){if(!lua_isinteger(L,n))luaL_argerror(L,n,"richiesto intero");return lua_tointeger(L,n);}
static int address(lua_State *L,int n){lua_Integer x=integer(L,n);luaL_argcheck(L,x>=0x08 && x<=0x77,n,"Indirizzo I2C non valido");return (int)x;}
static size_t length(lua_State *L,int n){lua_Integer x=integer(L,n);luaL_argcheck(L,x>0 && x<=I2C_TRANSFER_LIMIT,n,"lunghezza I2C non valida");return (size_t)x;}
static const uint8_t *payload(lua_State *L,int n,size_t *len){if(lua_type(L,n)!=LUA_TSTRING)luaL_argerror(L,n,"richiesta stringa binaria");const char *s=lua_tolstring(L,n,len);luaL_argcheck(L,*len>0 && *len<=I2C_TRANSFER_LIMIT,n,"payload I2C non valido");return (const uint8_t *)s;}
static zam_lua_context_t *ready(lua_State *L){zam_lua_context_t *c=context(L);if(c->gpio_closing)return (luaL_error(L,"I2C non disponibile durante cleanup"),NULL);if(!c->i2c_bus)return (luaL_error(L,"I2C non inizializzato"),NULL);if(c->i2c_device)return (luaL_error(L,"I2C non disponibile dopo errore cleanup"),NULL);return c;}
static int setup(lua_State *L){
    count(L,3);zam_lua_context_t *c=context(L);if(c->gpio_closing)return luaL_error(L,"I2C non disponibile durante cleanup");if(c->i2c_bus)return luaL_error(L,"I2C gia inizializzato");
    lua_Integer a=integer(L,1),b=integer(L,2),hz=integer(L,3);
    luaL_argcheck(L,a>=0 && a<ZAM_GPIO_TRACKED_PINS && zam_lua_gpio_allowed_output((int)a),1,"pin SDA non consentito");
    luaL_argcheck(L,b>=0 && b<ZAM_GPIO_TRACKED_PINS && zam_lua_gpio_allowed_output((int)b),2,"pin SCL non consentito");
    luaL_argcheck(L,a!=b,2,"SDA e SCL devono essere diversi");luaL_argcheck(L,hz==100000 || hz==400000,3,"frequenza I2C non valida");
    if(c->gpio_modes[a] || zam_lua_gpio_quarantined((int)a))return luaL_error(L,"GPIO%d gia utilizzato",(int)a);
    if(c->gpio_modes[b] || zam_lua_gpio_quarantined((int)b))return luaL_error(L,"GPIO%d gia utilizzato",(int)b);
    c->gpio_modes[a]=c->gpio_modes[b]=ZAM_GPIO_I2C;c->i2c_sda=(int)a;c->i2c_scl=(int)b;
    i2c_master_bus_config_t cfg={.i2c_port=-1,.sda_io_num=(int)a,.scl_io_num=(int)b,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7};
    i2c_master_bus_handle_t bus=NULL;esp_err_t e=i2c_new_master_bus(&cfg,&bus);
    if(e!=ESP_OK){c->gpio_modes[a]=c->gpio_modes[b]=ZAM_GPIO_FREE;return fail(L,e);}
    c->i2c_bus=bus;c->i2c_frequency=(unsigned)hz;return 0;
}
static int scan(lua_State *L){
    count(L,0);zam_lua_context_t *c=ready(L);
    if(c->i2c_frequency!=100000)return luaL_error(L,"scan I2C supportato solo a 100 kHz");
    lua_newtable(L);int n=1;
    for(int addr=0x08;addr<=0x77;addr++){
        if(c->callbacks->stop_requested(c->callbacks->context))return zam_lua_stop_error(L);
        esp_err_t e=i2c_master_probe(c->i2c_bus,(uint16_t)addr,I2C_PROBE_TIMEOUT_MS);
        if(e==ESP_OK){lua_pushinteger(L,addr);lua_rawseti(L,-2,n++);}else if(e!=ESP_ERR_NOT_FOUND)return fail(L,e);
    }return 1;
}
static int transfer(lua_State *L,int kind){
    count(L,kind==2?3:2);zam_lua_context_t *c=ready(L);int addr=address(L,1);size_t inlen=0,outlen=0;const uint8_t *in=NULL;uint8_t out[I2C_TRANSFER_LIMIT];
    if(kind!=1)in=payload(L,2,&inlen);
    if(kind==1)outlen=length(L,2);
    if(kind==2)outlen=length(L,3);
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=(uint16_t)addr,.scl_speed_hz=c->i2c_frequency};i2c_master_dev_handle_t dev=NULL;
    esp_err_t e=i2c_master_bus_add_device(c->i2c_bus,&cfg,&dev);if(e!=ESP_OK)return fail(L,e);c->i2c_device=dev;
    if(kind==0)e=i2c_master_transmit(dev,in,inlen,I2C_TRANSFER_TIMEOUT_MS);
    else if(kind==1)e=i2c_master_receive(dev,out,outlen,I2C_TRANSFER_TIMEOUT_MS);
    else e=i2c_master_transmit_receive(dev,in,inlen,out,outlen,I2C_TRANSFER_TIMEOUT_MS);
    esp_err_t release=i2c_master_bus_rm_device(dev);if(release==ESP_OK)c->i2c_device=NULL;else return luaL_error(L,"Rilascio dispositivo I2C fallito");
    if(e!=ESP_OK)return fail(L,e);
    if(kind){lua_pushlstring(L,(const char *)out,outlen);return 1;}
    return 0;
}
static int write_data(lua_State *L){return transfer(L,0);}static int read_data(lua_State *L){return transfer(L,1);}static int write_read(lua_State *L){return transfer(L,2);}
void zam_lua_i2c_register(lua_State *L){lua_newtable(L);lua_pushcfunction(L,setup);lua_setfield(L,-2,"setup");lua_pushcfunction(L,scan);lua_setfield(L,-2,"scan");lua_pushcfunction(L,write_data);lua_setfield(L,-2,"write");lua_pushcfunction(L,read_data);lua_setfield(L,-2,"read");lua_pushcfunction(L,write_read);lua_setfield(L,-2,"write_read");lua_setfield(L,-2,"i2c");}
bool zam_lua_i2c_cleanup(zam_lua_context_t *c){
    if(!c->i2c_bus)return true;
    if(c->i2c_device){if(i2c_master_bus_rm_device(c->i2c_device)!=ESP_OK)goto failed;c->i2c_device=NULL;}
    if(i2c_del_master_bus(c->i2c_bus)!=ESP_OK)goto failed;
    c->i2c_bus=NULL;c->gpio_modes[c->i2c_sda]=c->gpio_modes[c->i2c_scl]=ZAM_GPIO_FREE;return true;
failed:zam_lua_gpio_quarantine(c->i2c_sda);zam_lua_gpio_quarantine(c->i2c_scl);return false;
}
