/* cam_ctl.c -- read and set a UVC webcam's controls through DirectShow, no library needed.
 * Build: gcc -O2 -o cam_ctl.exe cam_ctl.c -lole32 -loleaut32 -lstrmiids -luuid
 * cam_ctl.exe                  lists every control: range, default, current value, auto or manual
 * cam_ctl.exe exposure=-5 ...  sets controls to manual at that value; name=auto hands one back to the camera */
#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <dshow.h>

typedef struct { const char*name; int isCam; long id; } Ctl;
static const Ctl CTLS[]={
    {"exposure",1,CameraControl_Exposure},{"focus",1,CameraControl_Focus},{"zoom",1,CameraControl_Zoom},{"iris",1,CameraControl_Iris},
    {"brightness",0,VideoProcAmp_Brightness},{"contrast",0,VideoProcAmp_Contrast},{"hue",0,VideoProcAmp_Hue},{"saturation",0,VideoProcAmp_Saturation},
    {"sharpness",0,VideoProcAmp_Sharpness},{"gamma",0,VideoProcAmp_Gamma},{"whitebalance",0,VideoProcAmp_WhiteBalance},{"backlight",0,VideoProcAmp_BacklightCompensation},{"gain",0,VideoProcAmp_Gain},
};
static IBaseFilter* findCam(const wchar_t*want){
    ICreateDevEnum*de=NULL; IEnumMoniker*em=NULL; IMoniker*m=NULL; IBaseFilter*f=NULL;
    if(FAILED(CoCreateInstance(&CLSID_SystemDeviceEnum,NULL,CLSCTX_INPROC_SERVER,&IID_ICreateDevEnum,(void**)&de))) return NULL;
    if(ICreateDevEnum_CreateClassEnumerator(de,&CLSID_VideoInputDeviceCategory,&em,0)!=S_OK) return NULL;
    while(IEnumMoniker_Next(em,1,&m,NULL)==S_OK){
        IPropertyBag*pb=NULL; VARIANT v; VariantInit(&v);
        if(SUCCEEDED(IMoniker_BindToStorage(m,NULL,NULL,&IID_IPropertyBag,(void**)&pb))&&SUCCEEDED(IPropertyBag_Read(pb,L"FriendlyName",&v,NULL))){
            if(wcsstr(v.bstrVal,want)){ IMoniker_BindToObject(m,NULL,NULL,&IID_IBaseFilter,(void**)&f); wprintf(L"camera: %ls\n",v.bstrVal); }
            VariantClear(&v); }
        if(pb) IPropertyBag_Release(pb);
        IMoniker_Release(m); if(f) break; }
    return f;
}
int main(int argc,char**argv){
    CoInitializeEx(NULL,COINIT_MULTITHREADED);
    IBaseFilter*f=findCam(L"HD Web Camera"); if(!f){ printf("camera not found\n"); return 1; }
    IAMCameraControl*cc=NULL; IAMVideoProcAmp*va=NULL;
    IBaseFilter_QueryInterface(f,&IID_IAMCameraControl,(void**)&cc);
    IBaseFilter_QueryInterface(f,&IID_IAMVideoProcAmp,(void**)&va);
    for(int a=1;a<argc;a++){ char*eq=strchr(argv[a],'='); if(!eq) continue; *eq=0; const char*val=eq+1;
        for(unsigned i=0;i<sizeof CTLS/sizeof CTLS[0];i++) if(!strcmp(CTLS[i].name,argv[a])){
            int isAuto=!strcmp(val,"auto"); long want=isAuto?0:atol(val); HRESULT hr=E_FAIL;
            if(CTLS[i].isCam&&cc){ long mn,mx,st,df,fl,cur,cf; if(isAuto&&SUCCEEDED(IAMCameraControl_GetRange(cc,CTLS[i].id,&mn,&mx,&st,&df,&fl))) want=df; else if(isAuto&&SUCCEEDED(IAMCameraControl_Get(cc,CTLS[i].id,&cur,&cf))) want=cur;
                hr=IAMCameraControl_Set(cc,CTLS[i].id,want,isAuto?CameraControl_Flags_Auto:CameraControl_Flags_Manual); }
            if(!CTLS[i].isCam&&va){ long mn,mx,st,df,fl; if(isAuto&&SUCCEEDED(IAMVideoProcAmp_GetRange(va,CTLS[i].id,&mn,&mx,&st,&df,&fl))) want=df;
                hr=IAMVideoProcAmp_Set(va,CTLS[i].id,want,isAuto?VideoProcAmp_Flags_Auto:VideoProcAmp_Flags_Manual); }
            printf("set %s=%s: %s\n",CTLS[i].name,val,SUCCEEDED(hr)?"ok":"FAILED"); }
    }
    for(unsigned i=0;i<sizeof CTLS/sizeof CTLS[0];i++){ long mn=0,mx=0,st=0,df=0,fl=0,cur=0,cf=0; HRESULT r1=E_FAIL,r2=E_FAIL;
        if(CTLS[i].isCam&&cc){ r1=IAMCameraControl_GetRange(cc,CTLS[i].id,&mn,&mx,&st,&df,&fl); r2=IAMCameraControl_Get(cc,CTLS[i].id,&cur,&cf); }
        if(!CTLS[i].isCam&&va){ r1=IAMVideoProcAmp_GetRange(va,CTLS[i].id,&mn,&mx,&st,&df,&fl); r2=IAMVideoProcAmp_Get(va,CTLS[i].id,&cur,&cf); }
        if(SUCCEEDED(r1)) printf("%-13s range %6ld..%-6ld step %-4ld default %-6ld now %-6ld %s\n",CTLS[i].name,mn,mx,st,df,SUCCEEDED(r2)?cur:-99999,(cf&1)?"auto":"manual");
        else printf("%-13s not available\n",CTLS[i].name); }
    return 0;
}
