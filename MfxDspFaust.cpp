/************************************************************************
 ************************************************************************
 FAUST API Architecture File
 Copyright (C) 2016 GRAME, Romain Michon, CCRMA - Stanford University
 Copyright (C) 2014-2017 GRAME, Centre National de Creation Musicale
 ---------------------------------------------------------------------
 
 This Architecture section is free software; you can redistribute it
 and/or modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 3 of
 the License, or (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; If not, see <http://www.gnu.org/licenses/>.
 
 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.
 ************************************************************************
 ************************************************************************/

#include <cmath>
#include <cstring>
#include <string.h>
#include <stdio.h>

#include "faust/misc.h"
#include "faust/gui/DecoratorUI.h"
#include "faust/gui/JSONUIDecoder.h"
#include "faust/dsp/dsp.h"
#include "faust/dsp/dsp-adapter.h"
#include "faust/gui/meta.h"

// we require macro declarations
#define FAUST_UIMACROS

// but we will ignore most of them
#define FAUST_ADDBUTTON(l,f)
#define FAUST_ADDCHECKBOX(l,f)
#define FAUST_ADDVERTICALSLIDER(l,f,i,a,b,s)
#define FAUST_ADDHORIZONTALSLIDER(l,f,i,a,b,s)
#define FAUST_ADDNUMENTRY(l,f,i,a,b,s)
#define FAUST_ADDVERTICALBARGRAPH(l,f,a,b)
#define FAUST_ADDHORIZONTALBARGRAPH(l,f,a,b)

//**************************************************************
// Soundfile handling
//**************************************************************

// Must be done before <<includeclass>> otherwise the 'Soundfile' type is not known

// So that the code uses JUCE audio file loading code
#include "faust/gui/SoundUI.h"

//**************************************************************
// OSC configuration (hardcoded for now...)
//**************************************************************

#define OSC_IP_ADDRESS  "192.168.1.112"
#define OSC_IN_PORT     "5510"
#define OSC_OUT_PORT    "5511"

//**************************************************************
// Polyphony
//**************************************************************

#include "faust/dsp/faust-poly-engine.h"

//**************************************************************
// Audio driver
//**************************************************************

#include "jack-dsp.h"

//**************************************************************
// Interface
//**************************************************************

// Nothing to add since jack-dsp.h contains MIDI

#include "faust/gui/OSCUI.h"
static void osc_compute_callback(void* arg)
{
    static_cast<OSCUI*>(arg)->endBundle();
}

#include "faust/dsp/llvm-dsp.h"

#include "MfxDspFaust.h"

using namespace std;

std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

static bool hasCompileOption(char* options, const char* option)
{
    char* token;
    const char* sep = " ";
    for (token = strtok(options, sep); token; token = strtok(nullptr, sep)) {
        if (strcmp(token, option) == 0) return true;
    }
    return false;
}

// DspFaust::DspFaust(bool auto_connect)
// {
//     audio* driver = nullptr;
//     // JACK has its own sample rate and buffer size
//     driver = new jackaudio_midi(auto_connect);
//     init(new mydsp(), driver);
// }
// 
// DspFaust::DspFaust(int sample_rate, int buffer_size, bool auto_connect)
// {
//     init(new mydsp(), createDriver(sample_rate, buffer_size, auto_connect));
// }

DspFaust::DspFaust(const string& dsp_content, int sample_rate, int buffer_size, bool auto_connect)
{
    string error_msg;
    
    // Is dsp_content a filename ?
    const char* argv[2] = { "-I", "faustlibraries" };
    fFactory = createDSPFactoryFromFile(dsp_content, 2, argv, "", error_msg, -1);
    if (!fFactory) {
        fprintf(stderr, "ERROR 1 : %s", error_msg.c_str());
        throw runtime_error(error_msg);
        // Is dsp_content a string ?
        // fFactory = createDSPFactoryFromString("FaustDSP", dsp_content, 0, nullptr, "", error_msg);
        // if (!fFactory) {
        //     fprintf(stderr, "ERROR : %s", error_msg.c_str());
        //     throw bad_alloc();
        // }
    }
    
    dsp* dsp = fFactory->createDSPInstance();
    if (!dsp) {
        fprintf(stderr, "Cannot allocate DSP instance\n");
        throw runtime_error("Cannot allocate DSP instance");
    }
    init(dsp, createDriver(sample_rate, buffer_size, auto_connect));
}

audio* DspFaust::createDriver(int sample_rate, int buffer_size, bool auto_connect)
{
    // JACK has its own sample rate and buffer size
    fprintf(stderr, "You are setting 'sample_rate' and 'buffer_size' with a driver that does not need it !\n");
    audio* driver = new jackaudio_midi(auto_connect);
    this->driver = static_cast<jackaudio_midi*>(driver);
    return driver;
}

void DspFaust::init(dsp* mono_dsp, audio* driver)
{
    midi_handler* handler;
    handler = static_cast<jackaudio_midi*>(driver);
    fMidiInterface = new MidiUI(handler);
    fPolyEngine = new FaustPolyEngine(mono_dsp, driver, handler);
    fPolyEngine->buildUserInterface(fMidiInterface);
    
    const char* argv[9];
    argv[0] = "Faust";  // TODO may be should retrieve the actual name
    argv[1] = "-xmit";
    argv[2] = "1";      // TODO retrieve that from command line or somewhere
    argv[3] = "-desthost";
    argv[4] = OSC_IP_ADDRESS;   // TODO same
    argv[5] = "-port";
    argv[6] = OSC_IN_PORT;      // TODO same
    argv[7] = "-outport";
    argv[8] = OSC_OUT_PORT;     // TODO same
    /*
     Deactivated for now (sometimes crashing)
     argv[9] = "-bundle";
     argv[10] = "1";             // TODO same
     */
    fOSCInterface = new OSCUI("Faust", 9, (char**)argv); // TODO fix name
    driver->addControlCallback(osc_compute_callback, fOSCInterface);
    fPolyEngine->buildUserInterface(fOSCInterface);
    
    // Retrieving DSP object 'compile_options'
    struct MyMeta : public Meta
    {
        string fCompileOptions;
        void declare(const char* key, const char* value)
        {
            if (strcmp(key, "compile_options") == 0) fCompileOptions = value;
        }
        MyMeta(){}
    };
    
    MyMeta meta;
    mono_dsp->metadata(&meta);
    bool is_double = hasCompileOption((char*)meta.fCompileOptions.c_str(), "-double");
    
    // Use bundle path
    fSoundInterface = new SoundUI(SoundUI::getBinaryPath(), -1, nullptr, is_double);
    fPolyEngine->buildUserInterface(fSoundInterface);
}

DspFaust::~DspFaust()
{
    delete fOSCInterface;
    delete fSoundInterface;
    delete fPolyEngine;
    deleteDSPFactory(static_cast<llvm_dsp_factory*>(fFactory));
    delete fMidiInterface;  // after deleting fPolyEngine;
}

bool DspFaust::start()
{
    fOSCInterface->run();
    if (!fMidiInterface->run()) {
        fprintf(stderr, "MIDI run error...\n");
    }
    return fPolyEngine->start();
}

void DspFaust::stop()
{
    fOSCInterface->stop();
    fMidiInterface->stop();
    fPolyEngine->stop();
}

bool DspFaust::configureOSC(int xmit, int inport, int outport, int errport, const char* address)
{
    if (isRunning()) {
        return false;
    } else {
        oscfaust::OSCControler::gXmit = xmit;
        fOSCInterface->setUDPPort(inport);
        fOSCInterface->setUDPOut(outport);
        fOSCInterface->setUDPErr(errport);
        fOSCInterface->setDestAddress(address);
        return true;
    }
}

bool DspFaust::isOSCOn()
{
    return true;
}

bool DspFaust::isRunning()
{
    return fPolyEngine->isRunning();
}

uintptr_t DspFaust::keyOn(int pitch, int velocity)
{
    return (uintptr_t)fPolyEngine->keyOn(pitch, velocity);
}

int DspFaust::keyOff(int pitch)
{
    return fPolyEngine->keyOff(pitch);
}

uintptr_t DspFaust::newVoice()
{
    return (uintptr_t)fPolyEngine->newVoice();
}

int DspFaust::deleteVoice(uintptr_t voice)
{
    return fPolyEngine->deleteVoice(voice);
}

void DspFaust::allNotesOff(bool hard)
{
    fPolyEngine->allNotesOff(hard);
}

void DspFaust::propagateMidi(int count, double time, int type, int channel, int data1, int data2)
{
    fPolyEngine->propagateMidi(count, time, type, channel, data1, data2);
}

const char* DspFaust::getJSONUI()
{
    return fPolyEngine->getJSONUI();
}

const char* DspFaust::getJSONMeta()
{
    return fPolyEngine->getJSONMeta();
}

void DspFaust::buildUserInterface(UI* ui_interface)
{
    fPolyEngine->buildUserInterface(ui_interface);
}

int DspFaust::getParamsCount()
{
    return fPolyEngine->getParamsCount();
}

void DspFaust::setParamValue(const char* address, float value)
{
    fPolyEngine->setParamValue(address, value);
}

void DspFaust::setParamValue(int id, float value)
{
    fPolyEngine->setParamValue(id, value);
}

float DspFaust::getParamValue(const char* address)
{
    return fPolyEngine->getParamValue(address);
}

float DspFaust::getParamValue(int id)
{
    return fPolyEngine->getParamValue(id);
}

void DspFaust::setVoiceParamValue(const char* address, uintptr_t voice, float value)
{
    fPolyEngine->setVoiceParamValue(address, voice, value);
}

void DspFaust::setVoiceParamValue(int id, uintptr_t voice, float value)
{
    fPolyEngine->setVoiceParamValue(id, voice, value);
}

float DspFaust::getVoiceParamValue(const char* address, uintptr_t voice)
{
    return fPolyEngine->getVoiceParamValue(address, voice);
}

float DspFaust::getVoiceParamValue(int id, uintptr_t voice)
{
    return fPolyEngine->getVoiceParamValue(id, voice);
}

const char* DspFaust::getParamAddress(int id)
{
    return fPolyEngine->getParamAddress(id);
}

const char* DspFaust::getVoiceParamAddress(int id, uintptr_t voice)
{
    return fPolyEngine->getVoiceParamAddress(id, voice);
}

float DspFaust::getParamMin(const char* address)
{
    return fPolyEngine->getParamMin(address);
}

float DspFaust::getParamMin(int id)
{
    return fPolyEngine->getParamMin(id);
}

float DspFaust::getParamMax(const char* address)
{
    return fPolyEngine->getParamMax(address);
}

float DspFaust::getParamMax(int id)
{
    return fPolyEngine->getParamMax(id);
}

float DspFaust::getParamInit(const char* address)
{
    return fPolyEngine->getParamInit(address);
}

float DspFaust::getParamInit(int id)
{
    return fPolyEngine->getParamInit(id);
}

const char* DspFaust::getMetadata(const char* address, const char* key)
{
    return fPolyEngine->getMetadata(address, key);
}

const char* DspFaust::getMetadata(int id, const char* key)
{
    return fPolyEngine->getMetadata(id, key);
}

void DspFaust::propagateAcc(int acc, float v)
{
    fPolyEngine->propagateAcc(acc, v);
}

void DspFaust::setAccConverter(int p, int acc, int curve, float amin, float amid, float amax)
{
    fPolyEngine->setAccConverter(p, acc, curve, amin, amid, amax);
}

void DspFaust::propagateGyr(int acc, float v)
{
    fPolyEngine->propagateGyr(acc, v);
}

void DspFaust::setGyrConverter(int p, int gyr, int curve, float amin, float amid, float amax)
{
    fPolyEngine->setGyrConverter(p, gyr, curve, amin, amid, amax);
}

float DspFaust::getCPULoad()
{
    return fPolyEngine->getCPULoad();
}

int DspFaust::getScreenColor()
{
    return fPolyEngine->getScreenColor();
}

#include <unistd.h>

extern "C"
{
  typedef struct {
    void (*openTabBox)(const char* label);
    void (*openHorizontalBox)(const char* label);
    void (*openVerticalBox)(const char* label);
    void (*closeBox)();
    void (*addButton)(const char* label, FAUSTFLOAT* zone);
    void (*addCheckButton)(const char* label, FAUSTFLOAT* zone);
    void (*addVerticalSlider)(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step);
    void (*addHorizontalSlider)(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step);
    void (*addNumEntry)(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step);
    void (*addHorizontalBargraph)(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max);
    void (*addVerticalBargraph)(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max);
    void (*addSoundfile)(const char* label, const char* soundpath, Soundfile** sf_zone);
    void (*declare)(FAUSTFLOAT* zone, const char* key, const char* val);  
  }  CLuaUI;
  class LuaUI : public GenericUI
  {    
  public:
    CLuaUI* lua_struct;
    LuaUI(CLuaUI* lua_struct) : lua_struct(lua_struct) {}
    virtual ~LuaUI() {}        
    virtual void openTabBox(const char* label) { if (lua_struct->openTabBox != NULL) lua_struct->openTabBox(label); }
    virtual void openHorizontalBox(const char* label) { if (lua_struct->openHorizontalBox != NULL) lua_struct->openHorizontalBox(label); }
    virtual void openVerticalBox(const char* label) { if (lua_struct->openVerticalBox != NULL) lua_struct->openVerticalBox(label); }
    virtual void closeBox() { if (lua_struct->closeBox != NULL) lua_struct->closeBox(); }        
    virtual void addButton(const char* label, FAUSTFLOAT* zone) { if (lua_struct->addButton != NULL) lua_struct->addButton(label, zone); }
    virtual void addCheckButton(const char* label, FAUSTFLOAT* zone) { if (lua_struct->addCheckButton != NULL) lua_struct->addCheckButton(label, zone); }
    virtual void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) { if (lua_struct->addVerticalSlider != NULL) lua_struct->addVerticalSlider(label, zone, init, min, max, step); }
    virtual void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) { if (lua_struct->addHorizontalSlider != NULL) lua_struct->addHorizontalSlider(label, zone, init, min, max, step); }
    virtual void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) { if (lua_struct->addNumEntry != NULL) lua_struct->addNumEntry(label, zone, init, min, max, step); }    
    virtual void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) { if (lua_struct->addHorizontalBargraph != NULL) lua_struct->addHorizontalBargraph(label, zone, min, max); }
    virtual void addVerticalBargraph(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) { if (lua_struct->addVerticalBargraph != NULL) lua_struct->addVerticalBargraph(label, zone, min, max); }    
    virtual void addSoundfile(const char* label, const char* soundpath, Soundfile** sf_zone) { if (lua_struct->addSoundfile != NULL) lua_struct->addSoundfile(label, soundpath, sf_zone); }    
    virtual void declare(FAUSTFLOAT* zone, const char* key, const char* val) { if (lua_struct->declare != NULL) lua_struct->declare(zone, key, val); }    
  };
}

struct MfxDspFaust : DspFaust
{ 
  int check = 42;
  MfxDspFaust (const char * file) : DspFaust(string(file), 44100, 512, false)
  {
    std::cout << "Hi you :)" << '\n';
  }
  ~MfxDspFaust()
  {
    std::cout << "Hey!" << '\n';
  }
  
  void buildCLuaInterface(CLuaUI* lua_struct)
  {
    LuaUI lua(lua_struct);
    buildUserInterface(&lua);
  }  
};

extern "C"
{
typedef void lua_DspFaust;
lua_DspFaust* lua_newDspfaust(const char * file, char * error_msg);
void lua_startDspfaust(lua_DspFaust* dsp);
void lua_stopDspfaust(lua_DspFaust* dsp);
void lua_buildCLuaInterface(lua_DspFaust* dsp, CLuaUI* lua_struct);
float* lua_getDspMemory(lua_DspFaust* dsp);
void lua_setRingbuffer(lua_DspFaust* dsp, ringbuffer_t* rb);

lua_DspFaust* lua_newDspfaust(const char * file, char * error_msg)
{
    printf("Juste ici!\n");
    printf("%s\n", file);
    try {
      MfxDspFaust* dsp = new MfxDspFaust(file);
      error_msg[0] = '\0';
      return static_cast<lua_DspFaust*>(dsp);
    } catch (runtime_error& e) {
      strcpy(error_msg, e.what());
      return static_cast<lua_DspFaust*>(nullptr);
    }
}

void lua_startDspfaust(lua_DspFaust* dsp)
{
    static_cast<MfxDspFaust*>(dsp)->start();
}


void lua_stopDspfaust(lua_DspFaust* dsp)
{
    auto _dsp = static_cast<MfxDspFaust*>(dsp);
    _dsp->stop();
    delete _dsp;
    dsp = nullptr;
}

void lua_buildCLuaInterface(lua_DspFaust* dsp, CLuaUI* lua_struct)
{
  static_cast<MfxDspFaust*>(dsp)->buildCLuaInterface(lua_struct);
}

float* lua_getDspMemory(lua_DspFaust* dsp)
{
  return static_cast<MfxDspFaust*>(dsp)->memory;
}

void lua_setRingbuffer(lua_DspFaust* dsp, ringbuffer_t* rb)
{
  auto _dsp = static_cast<MfxDspFaust*>(dsp);
  _dsp->driver->rb = rb;
}

int main(int argc, char* argv[])
{
    if (argc == 1) {
        printf("./dynamic-api <foo.dsp> \n");
        exit(-1);
    }
    // DspFaust* dsp = new DspFaust(argv[0], 44100, 512);
    // dsp->start();
    char error_msg[2048];
    auto dsp = lua_newDspfaust(argv[1], error_msg);
    lua_startDspfaust(dsp);
    printf("Type 'q' to quit\n");
    char c;
    while ((c = getchar()) && (c != 'q')) { usleep(100000); }
    // dsp->stop();
    lua_stopDspfaust(dsp);
    // delete dsp;
}

}