/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-9 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

#include "../../../core/juce_TargetPlatform.h"
#include "../../../../juce_Config.h"

#if JUCE_PLUGINHOST_VST

#if (defined (_WIN32) || defined (_WIN64))
 #undef _WIN32_WINNT
 #define _WIN32_WINNT 0x500
 #undef STRICT
 #define STRICT
 #include <windows.h>
 #include <float.h>
 #pragma warning (disable : 4312 4355)
#elif defined (LINUX) || defined (__linux__)
 #include <float.h>
 #include <sys/time.h>
 #include <X11/Xlib.h>
 #include <X11/Xutil.h>
 #include <X11/Xatom.h>
 #undef Font
 #undef KeyPress
 #undef Drawable
 #undef Time
#else
 #ifndef JUCE_MAC_VST_INCLUDED
  // On the mac, this file needs to be compiled indirectly, by using
  // juce_VSTPluginFormat.mm instead - that wraps it as an objective-C file for cocoa
  #error
 #endif
 #include <Cocoa/Cocoa.h>
 #include <Carbon/Carbon.h>
#endif

//==============================================================================
#include "../../../core/juce_StandardHeader.h"

#if ! (JUCE_MAC && JUCE_64BIT)

BEGIN_JUCE_NAMESPACE

#include "juce_VSTPluginFormat.h"
#include "../../../threads/juce_Process.h"
#include "../../../threads/juce_ScopedLock.h"
#include "../../../core/juce_Random.h"
#include "../../../io/files/juce_DirectoryIterator.h"
#include "../../../events/juce_Timer.h"
#include "../../../events/juce_AsyncUpdater.h"
#include "../../../events/juce_MessageManager.h"
#include "../../../gui/components/layout/juce_ComponentMovementWatcher.h"
#include "../../../application/juce_Application.h"
#include "../../../core/juce_PlatformUtilities.h"

#if JUCE_MAC && JUCE_SUPPORT_CARBON
#include "../../../native/mac/juce_mac_CarbonViewWrapperComponent.h"
#endif

//==============================================================================
#undef PRAGMA_ALIGN_SUPPORTED
#define VST_FORCE_DEPRECATED 0

#ifdef _MSC_VER
  #pragma warning (push)
  #pragma warning (disable: 4996)
#endif

/*  Obviously you're going to need the Steinberg vstsdk2.4 folder in
    your include path if you want to add VST support.

    If you're not interested in VSTs, you can disable them by changing the
    JUCE_PLUGINHOST_VST flag in juce_Config.h
*/
#include "pluginterfaces/vst2.x/aeffectx.h"

#ifdef _MSC_VER
  #pragma warning (pop)
#endif

//==============================================================================
#if JUCE_LINUX
 #define Font       JUCE_NAMESPACE::Font
 #define KeyPress   JUCE_NAMESPACE::KeyPress
 #define Drawable   JUCE_NAMESPACE::Drawable
 #define Time       JUCE_NAMESPACE::Time
#endif

#include "../juce_PluginDescription.h"
#include "juce_VSTMidiEventList.h"

#if ! JUCE_WIN32
 #define _fpreset()
 #define _clearfp()
#endif

extern void juce_callAnyTimersSynchronously();


//==============================================================================
const int fxbVersionNum = 1;

struct fxProgram
{
    long chunkMagic;        // 'CcnK'
    long byteSize;          // of this chunk, excl. magic + byteSize
    long fxMagic;           // 'FxCk'
    long version;
    long fxID;              // fx unique id
    long fxVersion;
    long numParams;
    char prgName[28];
    float params[1];        // variable no. of parameters
};

struct fxSet
{
    long chunkMagic;        // 'CcnK'
    long byteSize;          // of this chunk, excl. magic + byteSize
    long fxMagic;           // 'FxBk'
    long version;
    long fxID;              // fx unique id
    long fxVersion;
    long numPrograms;
    char future[128];
    fxProgram programs[1];  // variable no. of programs
};

struct fxChunkSet
{
    long chunkMagic;        // 'CcnK'
    long byteSize;          // of this chunk, excl. magic + byteSize
    long fxMagic;           // 'FxCh', 'FPCh', or 'FBCh'
    long version;
    long fxID;              // fx unique id
    long fxVersion;
    long numPrograms;
    char future[128];
    long chunkSize;
    char chunk[8];          // variable
};

struct fxProgramSet
{
    long chunkMagic;        // 'CcnK'
    long byteSize;          // of this chunk, excl. magic + byteSize
    long fxMagic;           // 'FxCh', 'FPCh', or 'FBCh'
    long version;
    long fxID;              // fx unique id
    long fxVersion;
    long numPrograms;
    char name[28];
    long chunkSize;
    char chunk[8];          // variable
};


static long vst_swap (const long x) throw()
{
  #ifdef JUCE_LITTLE_ENDIAN
    return (long) ByteOrder::swap ((uint32) x);
  #else
    return x;
  #endif
}

static float vst_swapFloat (const float x) throw()
{
  #ifdef JUCE_LITTLE_ENDIAN
    union { uint32 asInt; float asFloat; } n;
    n.asFloat = x;
    n.asInt = ByteOrder::swap (n.asInt);
    return n.asFloat;
  #else
    return x;
  #endif
}

//==============================================================================
typedef AEffect* (*MainCall) (audioMasterCallback);

static VstIntPtr VSTCALLBACK audioMaster (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

static int shellUIDToCreate = 0;
static int insideVSTCallback = 0;

class VSTPluginWindow;

//==============================================================================
// Change this to disable logging of various VST activities
#ifndef VST_LOGGING
  #define VST_LOGGING 1
#endif

#if VST_LOGGING
 #define log(a) Logger::writeToLog(a);
#else
 #define log(a)
#endif

//==============================================================================
#if JUCE_MAC && JUCE_PPC
static void* NewCFMFromMachO (void* const machofp) throw()
{
    void* result = juce_malloc (8);

    ((void**) result)[0] = machofp;
    ((void**) result)[1] = result;

    return result;
}
#endif

//==============================================================================
#if JUCE_LINUX

extern Display* display;
extern XContext improbableNumber;

typedef void (*EventProcPtr) (XEvent* ev);

static bool xErrorTriggered;

static int temporaryErrorHandler (Display*, XErrorEvent*)
{
    xErrorTriggered = true;
    return 0;
}

static int getPropertyFromXWindow (Window handle, Atom atom)
{
    XErrorHandler oldErrorHandler = XSetErrorHandler (temporaryErrorHandler);
    xErrorTriggered = false;

    int userSize;
    unsigned long bytes, userCount;
    unsigned char* data;
    Atom userType;

    XGetWindowProperty (display, handle, atom, 0, 1, false, AnyPropertyType,
                        &userType,  &userSize, &userCount, &bytes, &data);

    XSetErrorHandler (oldErrorHandler);

    return (userCount == 1 && ! xErrorTriggered) ? *(int*) data
                                                 : 0;
}

static Window getChildWindow (Window windowToCheck)
{
    Window rootWindow, parentWindow;
    Window* childWindows;
    unsigned int numChildren;

    XQueryTree (display,
                windowToCheck,
                &rootWindow,
                &parentWindow,
                &childWindows,
                &numChildren);

    if (numChildren > 0)
        return childWindows [0];

    return 0;
}

static void translateJuceToXButtonModifiers (const MouseEvent& e, XEvent& ev) throw()
{
    if (e.mods.isLeftButtonDown())
    {
        ev.xbutton.button = Button1;
        ev.xbutton.state |= Button1Mask;
    }
    else if (e.mods.isRightButtonDown())
    {
        ev.xbutton.button = Button3;
        ev.xbutton.state |= Button3Mask;
    }
    else if (e.mods.isMiddleButtonDown())
    {
        ev.xbutton.button = Button2;
        ev.xbutton.state |= Button2Mask;
    }
}

static void translateJuceToXMotionModifiers (const MouseEvent& e, XEvent& ev) throw()
{
    if (e.mods.isLeftButtonDown())
        ev.xmotion.state |= Button1Mask;
    else if (e.mods.isRightButtonDown())
        ev.xmotion.state |= Button3Mask;
    else if (e.mods.isMiddleButtonDown())
        ev.xmotion.state |= Button2Mask;
}

static void translateJuceToXCrossingModifiers (const MouseEvent& e, XEvent& ev) throw()
{
    if (e.mods.isLeftButtonDown())
        ev.xcrossing.state |= Button1Mask;
    else if (e.mods.isRightButtonDown())
        ev.xcrossing.state |= Button3Mask;
    else if (e.mods.isMiddleButtonDown())
        ev.xcrossing.state |= Button2Mask;
}

static void translateJuceToXMouseWheelModifiers (const MouseEvent& e, const float increment, XEvent& ev) throw()
{
    if (increment < 0)
    {
        ev.xbutton.button = Button5;
        ev.xbutton.state |= Button5Mask;
    }
    else if (increment > 0)
    {
        ev.xbutton.button = Button4;
        ev.xbutton.state |= Button4Mask;
    }
}

#endif

//==============================================================================
static VoidArray activeModules;

//==============================================================================
class ModuleHandle    : public ReferenceCountedObject
{
public:
    //==============================================================================
    File file;
    MainCall moduleMain;
    String pluginName;

    //==============================================================================
    static ModuleHandle* findOrCreateModule (const File& file)
    {
        for (int i = activeModules.size(); --i >= 0;)
        {
            ModuleHandle* const module = (ModuleHandle*) activeModules.getUnchecked(i);

            if (module->file == file)
                return module;
        }

        _fpreset(); // (doesn't do any harm)
        ++insideVSTCallback;
        shellUIDToCreate = 0;

        log ("Attempting to load VST: " + file.getFullPathName());

        ScopedPointer <ModuleHandle> m (new ModuleHandle (file));

        if (! m->open())
            m = 0;

        --insideVSTCallback;
        _fpreset(); // (doesn't do any harm)

        return m.release();
    }

    //==============================================================================
    ModuleHandle (const File& file_)
        : file (file_),
          moduleMain (0),
#if JUCE_WIN32 || JUCE_LINUX
          hModule (0)
#elif JUCE_MAC
          fragId (0),
          resHandle (0),
          bundleRef (0),
          resFileId (0)
#endif
    {
        activeModules.add (this);

#if JUCE_WIN32 || JUCE_LINUX
        fullParentDirectoryPathName = file_.getParentDirectory().getFullPathName();
#elif JUCE_MAC
        FSRef ref;
        PlatformUtilities::makeFSRefFromPath (&ref, file_.getParentDirectory().getFullPathName());
        FSGetCatalogInfo (&ref, kFSCatInfoNone, 0, 0, &parentDirFSSpec, 0);
#endif
    }

    ~ModuleHandle()
    {
        activeModules.removeValue (this);

        close();
    }

    //==============================================================================
    juce_UseDebuggingNewOperator

    //==============================================================================
#if JUCE_WIN32 || JUCE_LINUX
    void* hModule;
    String fullParentDirectoryPathName;

    bool open()
    {
#if JUCE_WIN32
        static bool timePeriodSet = false;

        if (! timePeriodSet)
        {
            timePeriodSet = true;
            timeBeginPeriod (2);
        }
#endif

        pluginName = file.getFileNameWithoutExtension();

        hModule = PlatformUtilities::loadDynamicLibrary (file.getFullPathName());

        moduleMain = (MainCall) PlatformUtilities::getProcedureEntryPoint (hModule, "VSTPluginMain");

        if (moduleMain == 0)
            moduleMain = (MainCall) PlatformUtilities::getProcedureEntryPoint (hModule, "main");

        return moduleMain != 0;
    }

    void close()
    {
        _fpreset(); // (doesn't do any harm)

        PlatformUtilities::freeDynamicLibrary (hModule);
    }

    void closeEffect (AEffect* eff)
    {
        eff->dispatcher (eff, effClose, 0, 0, 0, 0);
    }

#else
    CFragConnectionID fragId;
    Handle resHandle;
    CFBundleRef bundleRef;
    FSSpec parentDirFSSpec;
    short resFileId;

    bool open()
    {
        bool ok = false;
        const String filename (file.getFullPathName());

        if (file.hasFileExtension (T(".vst")))
        {
            const char* const utf8 = filename.toUTF8();
            CFURLRef url = CFURLCreateFromFileSystemRepresentation (0, (const UInt8*) utf8,
                                                                    strlen (utf8), file.isDirectory());

            if (url != 0)
            {
                bundleRef = CFBundleCreate (kCFAllocatorDefault, url);
                CFRelease (url);

                if (bundleRef != 0)
                {
                    if (CFBundleLoadExecutable (bundleRef))
                    {
                        moduleMain = (MainCall) CFBundleGetFunctionPointerForName (bundleRef, CFSTR("main_macho"));

                        if (moduleMain == 0)
                            moduleMain = (MainCall) CFBundleGetFunctionPointerForName (bundleRef, CFSTR("VSTPluginMain"));

                        if (moduleMain != 0)
                        {
                            CFTypeRef name = CFBundleGetValueForInfoDictionaryKey (bundleRef, CFSTR("CFBundleName"));

                            if (name != 0)
                            {
                                if (CFGetTypeID (name) == CFStringGetTypeID())
                                {
                                    char buffer[1024];

                                    if (CFStringGetCString ((CFStringRef) name, buffer, sizeof (buffer), CFStringGetSystemEncoding()))
                                        pluginName = buffer;
                                }
                            }

                            if (pluginName.isEmpty())
                                pluginName = file.getFileNameWithoutExtension();

                            resFileId = CFBundleOpenBundleResourceMap (bundleRef);

                            ok = true;
                        }
                    }

                    if (! ok)
                    {
                        CFBundleUnloadExecutable (bundleRef);
                        CFRelease (bundleRef);
                        bundleRef = 0;
                    }
                }
            }
        }
#if JUCE_PPC
        else
        {
            FSRef fn;

            if (FSPathMakeRef ((UInt8*) (const char*) filename, &fn, 0) == noErr)
            {
                resFileId = FSOpenResFile (&fn, fsRdPerm);

                if (resFileId != -1)
                {
                    const int numEffs = Count1Resources ('aEff');

                    for (int i = 0; i < numEffs; ++i)
                    {
                        resHandle = Get1IndResource ('aEff', i + 1);

                        if (resHandle != 0)
                        {
                            OSType type;
                            Str255 name;
                            SInt16 id;
                            GetResInfo (resHandle, &id, &type, name);
                            pluginName = String ((const char*) name + 1, name[0]);
                            DetachResource (resHandle);
                            HLock (resHandle);

                            Ptr ptr;
                            Str255 errorText;

                            OSErr err = GetMemFragment (*resHandle, GetHandleSize (resHandle),
                                                        name, kPrivateCFragCopy,
                                                        &fragId, &ptr, errorText);

                            if (err == noErr)
                            {
                                moduleMain = (MainCall) newMachOFromCFM (ptr);
                                ok = true;
                            }
                            else
                            {
                                HUnlock (resHandle);
                            }

                            break;
                        }
                    }

                    if (! ok)
                        CloseResFile (resFileId);
                }
            }
        }
#endif

        return ok;
    }

    void close()
    {
#if JUCE_PPC
        if (fragId != 0)
        {
            if (moduleMain != 0)
                disposeMachOFromCFM ((void*) moduleMain);

            CloseConnection (&fragId);
            HUnlock (resHandle);

            if (resFileId != 0)
                CloseResFile (resFileId);
        }
        else
#endif
        if (bundleRef != 0)
        {
            CFBundleCloseBundleResourceMap (bundleRef, resFileId);

            if (CFGetRetainCount (bundleRef) == 1)
                CFBundleUnloadExecutable (bundleRef);

            if (CFGetRetainCount (bundleRef) > 0)
                CFRelease (bundleRef);
        }
    }

    void closeEffect (AEffect* eff)
    {
#if JUCE_PPC
        if (fragId != 0)
        {
            VoidArray thingsToDelete;
            thingsToDelete.add ((void*) eff->dispatcher);
            thingsToDelete.add ((void*) eff->process);
            thingsToDelete.add ((void*) eff->setParameter);
            thingsToDelete.add ((void*) eff->getParameter);
            thingsToDelete.add ((void*) eff->processReplacing);

            eff->dispatcher (eff, effClose, 0, 0, 0, 0);

            for (int i = thingsToDelete.size(); --i >= 0;)
                disposeMachOFromCFM (thingsToDelete[i]);
        }
        else
#endif
        {
            eff->dispatcher (eff, effClose, 0, 0, 0, 0);
        }
    }

#if JUCE_PPC
    static void* newMachOFromCFM (void* cfmfp)
    {
        if (cfmfp == 0)
            return 0;

        UInt32* const mfp = (UInt32*) juce_malloc (sizeof (UInt32) * 6);

        mfp[0] = 0x3d800000 | ((UInt32) cfmfp >> 16);
        mfp[1] = 0x618c0000 | ((UInt32) cfmfp & 0xffff);
        mfp[2] = 0x800c0000;
        mfp[3] = 0x804c0004;
        mfp[4] = 0x7c0903a6;
        mfp[5] = 0x4e800420;

        MakeDataExecutable (mfp, sizeof (UInt32) * 6);
        return mfp;
    }

    static void disposeMachOFromCFM (void* ptr)
    {
        juce_free (ptr);
    }

    void coerceAEffectFunctionCalls (AEffect* eff)
    {
        if (fragId != 0)
        {
            eff->dispatcher = (AEffectDispatcherProc) newMachOFromCFM ((void*) eff->dispatcher);
            eff->process = (AEffectProcessProc) newMachOFromCFM ((void*) eff->process);
            eff->setParameter = (AEffectSetParameterProc) newMachOFromCFM ((void*) eff->setParameter);
            eff->getParameter = (AEffectGetParameterProc) newMachOFromCFM ((void*) eff->getParameter);
            eff->processReplacing = (AEffectProcessProc) newMachOFromCFM ((void*) eff->processReplacing);
        }
    }
#endif

#endif
};


//==============================================================================
/**
    An instance of a plugin, created by a VSTPluginFormat.

*/
class VSTPluginInstance     : public AudioPluginInstance,
                              private Timer,
                              private AsyncUpdater
{
public:
    //==============================================================================
    ~VSTPluginInstance();

    //==============================================================================
    // AudioPluginInstance methods:

    void fillInPluginDescription (PluginDescription& desc) const
    {
        desc.name = name;
        desc.fileOrIdentifier = module->file.getFullPathName();
        desc.uid = getUID();
        desc.lastFileModTime = module->file.getLastModificationTime();
        desc.pluginFormatName = "VST";
        desc.category = getCategory();

        {
            char buffer [kVstMaxVendorStrLen + 8];
            zerostruct (buffer);
            dispatch (effGetVendorString, 0, 0, buffer, 0);
            desc.manufacturerName = buffer;
        }

        desc.version = getVersion();
        desc.numInputChannels = getNumInputChannels();
        desc.numOutputChannels = getNumOutputChannels();
        desc.isInstrument = (effect != 0 && (effect->flags & effFlagsIsSynth) != 0);
    }

    const String getName() const            { return name; }
    int getUID() const throw();
    bool acceptsMidi() const                { return wantsMidiMessages; }
    bool producesMidi() const               { return dispatch (effCanDo, 0, 0, (void*) "sendVstMidiEvent", 0) > 0; }

    //==============================================================================
    // AudioProcessor methods:

    void prepareToPlay (double sampleRate, int estimatedSamplesPerBlock);
    void releaseResources();
    void processBlock (AudioSampleBuffer& buffer,
                       MidiBuffer& midiMessages);

    AudioProcessorEditor* createEditor();

    const String getInputChannelName (const int index) const;
    bool isInputChannelStereoPair (int index) const;

    const String getOutputChannelName (const int index) const;
    bool isOutputChannelStereoPair (int index) const;

    //==============================================================================
    int getNumParameters()                              { return effect != 0 ? effect->numParams : 0; }
    float getParameter (int index);
    void setParameter (int index, float newValue);
    const String getParameterName (int index);
    const String getParameterText (int index);
    bool isParameterAutomatable (int index) const;

    //==============================================================================
    int getNumPrograms()                                { return effect != 0 ? effect->numPrograms : 0; }
    int getCurrentProgram()                             { return dispatch (effGetProgram, 0, 0, 0, 0); }
    void setCurrentProgram (int index);
    const String getProgramName (int index);
    void changeProgramName (int index, const String& newName);

    //==============================================================================
    void getStateInformation (MemoryBlock& destData);
    void getCurrentProgramStateInformation (MemoryBlock& destData);
    void setStateInformation (const void* data, int sizeInBytes);
    void setCurrentProgramStateInformation (const void* data, int sizeInBytes);

    //==============================================================================
    void timerCallback();
    void handleAsyncUpdate();
    VstIntPtr handleCallback (VstInt32 opcode, VstInt32 index, VstInt32 value, void *ptr, float opt);

    //==============================================================================
    juce_UseDebuggingNewOperator

private:
    friend class VSTPluginWindow;
    friend class VSTPluginFormat;

    AEffect* effect;
    String name;
    CriticalSection lock;
    bool wantsMidiMessages, initialised, isPowerOn;
    mutable StringArray programNames;
    AudioSampleBuffer tempBuffer;
    CriticalSection midiInLock;
    MidiBuffer incomingMidi;
    VSTMidiEventList midiEventsToSend;
    VstTimeInfo vstHostTime;
    HeapBlock <float*> channels;

    ReferenceCountedObjectPtr <ModuleHandle> module;

    //==============================================================================
    int dispatch (const int opcode, const int index, const int value, void* const ptr, float opt) const;
    bool restoreProgramSettings (const fxProgram* const prog);
    const String getCurrentProgramName();
    void setParamsInProgramBlock (fxProgram* const prog) throw();
    void updateStoredProgramNames();
    void initialise();
    void handleMidiFromPlugin (const VstEvents* const events);
    void createTempParameterStore (MemoryBlock& dest);
    void restoreFromTempParameterStore (const MemoryBlock& mb);
    const String getParameterLabel (int index) const;

    bool usesChunks() const throw()         { return effect != 0 && (effect->flags & effFlagsProgramChunks) != 0; }
    void getChunkData (MemoryBlock& mb, bool isPreset, int maxSizeMB) const;
    void setChunkData (const char* data, int size, bool isPreset);
    bool loadFromFXBFile (const void* data, int numBytes);
    bool saveToFXBFile (MemoryBlock& dest, bool isFXB, int maxSizeMB);

    int getVersionNumber() const throw()    { return effect != 0 ? effect->version : 0; }
    const String getVersion() const throw();
    const String getCategory() const throw();

    bool hasEditor() const throw()          { return effect != 0 && (effect->flags & effFlagsHasEditor) != 0; }
    void setPower (const bool on);

    VSTPluginInstance (const ReferenceCountedObjectPtr <ModuleHandle>& module);
};

//==============================================================================
VSTPluginInstance::VSTPluginInstance (const ReferenceCountedObjectPtr <ModuleHandle>& module_)
    : effect (0),
      wantsMidiMessages (false),
      initialised (false),
      isPowerOn (false),
      tempBuffer (1, 1),
      module (module_)
{
    try
    {
        _fpreset();

        ++insideVSTCallback;

        name = module->pluginName;
        log (T("Creating VST instance: ") + name);

#if JUCE_MAC
        if (module->resFileId != 0)
            UseResFile (module->resFileId);

#if JUCE_PPC
        if (module->fragId != 0)
        {
            static void* audioMasterCoerced = 0;
            if (audioMasterCoerced == 0)
                audioMasterCoerced = NewCFMFromMachO ((void*) &audioMaster);

            effect = module->moduleMain ((audioMasterCallback) audioMasterCoerced);
        }
        else
#endif
#endif
        {
            effect = module->moduleMain (&audioMaster);
        }

        --insideVSTCallback;

        if (effect != 0 && effect->magic == kEffectMagic)
        {
#if JUCE_PPC
            module->coerceAEffectFunctionCalls (effect);
#endif

            jassert (effect->resvd2 == 0);
            jassert (effect->object != 0);

            _fpreset(); // some dodgy plugs fuck around with this
        }
        else
        {
            effect = 0;
        }
    }
    catch (...)
    {
        --insideVSTCallback;
    }
}

VSTPluginInstance::~VSTPluginInstance()
{
    {
        const ScopedLock sl (lock);

        jassert (insideVSTCallback == 0);

        if (effect != 0 && effect->magic == kEffectMagic)
        {
            try
            {
#if JUCE_MAC
                if (module->resFileId != 0)
                    UseResFile (module->resFileId);
#endif

                // Must delete any editors before deleting the plugin instance!
                jassert (getActiveEditor() == 0);

                _fpreset(); // some dodgy plugs fuck around with this

                module->closeEffect (effect);
            }
            catch (...)
            {}
        }

        module = 0;
        effect = 0;
    }
}

//==============================================================================
void VSTPluginInstance::initialise()
{
    if (initialised || effect == 0)
        return;

    log (T("Initialising VST: ") + module->pluginName);
    initialised = true;

    dispatch (effIdentify, 0, 0, 0, 0);

    // this code would ask the plugin for its name, but so few plugins
    // actually bother implementing this correctly, that it's better to
    // just ignore it and use the file name instead.
/*    {
        char buffer [256];
        zerostruct (buffer);
        dispatch (effGetEffectName, 0, 0, buffer, 0);

        name = String (buffer).trim();
        if (name.isEmpty())
            name = module->pluginName;
    }
*/

    if (getSampleRate() > 0)
        dispatch (effSetSampleRate, 0, 0, 0, (float) getSampleRate());

    if (getBlockSize() > 0)
        dispatch (effSetBlockSize, 0, jmax (32, getBlockSize()), 0, 0);

    dispatch (effOpen, 0, 0, 0, 0);

    setPlayConfigDetails (effect->numInputs, effect->numOutputs,
                          getSampleRate(), getBlockSize());

    if (getNumPrograms() > 1)
        setCurrentProgram (0);
    else
        dispatch (effSetProgram, 0, 0, 0, 0);

    int i;
    for (i = effect->numInputs; --i >= 0;)
        dispatch (effConnectInput, i, 1, 0, 0);

    for (i = effect->numOutputs; --i >= 0;)
        dispatch (effConnectOutput, i, 1, 0, 0);

    updateStoredProgramNames();

    wantsMidiMessages = dispatch (effCanDo, 0, 0, (void*) "receiveVstMidiEvent", 0) > 0;

    setLatencySamples (effect->initialDelay);
}


//==============================================================================
void VSTPluginInstance::prepareToPlay (double sampleRate_,
                                       int samplesPerBlockExpected)
{
    setPlayConfigDetails (effect->numInputs, effect->numOutputs,
                          sampleRate_, samplesPerBlockExpected);

    setLatencySamples (effect->initialDelay);

    channels.calloc (jmax (16, getNumOutputChannels(), getNumInputChannels()) + 2);

    vstHostTime.tempo = 120.0;
    vstHostTime.timeSigNumerator = 4;
    vstHostTime.timeSigDenominator = 4;
    vstHostTime.sampleRate = sampleRate_;
    vstHostTime.samplePos = 0;
    vstHostTime.flags = kVstNanosValid;  /*| kVstTransportPlaying | kVstTempoValid | kVstTimeSigValid*/;

    initialise();

    if (initialised)
    {
        wantsMidiMessages = wantsMidiMessages
                                || (dispatch (effCanDo, 0, 0, (void*) "receiveVstMidiEvent", 0) > 0);

        if (wantsMidiMessages)
            midiEventsToSend.ensureSize (256);
        else
            midiEventsToSend.freeEvents();

        incomingMidi.clear();

        dispatch (effSetSampleRate, 0, 0, 0, (float) sampleRate_);
        dispatch (effSetBlockSize, 0, jmax (16, samplesPerBlockExpected), 0, 0);

        tempBuffer.setSize (jmax (1, effect->numOutputs), samplesPerBlockExpected);

        if (! isPowerOn)
            setPower (true);

        // dodgy hack to force some plugins to initialise the sample rate..
        if ((! hasEditor()) && getNumParameters() > 0)
        {
            const float old = getParameter (0);
            setParameter (0, (old < 0.5f) ? 1.0f : 0.0f);
            setParameter (0, old);
        }

        dispatch (effStartProcess, 0, 0, 0, 0);
    }
}

void VSTPluginInstance::releaseResources()
{
    if (initialised)
    {
        dispatch (effStopProcess, 0, 0, 0, 0);
        setPower (false);
    }

    tempBuffer.setSize (1, 1);
    incomingMidi.clear();

    midiEventsToSend.freeEvents();
    channels.free();
}

void VSTPluginInstance::processBlock (AudioSampleBuffer& buffer,
                                      MidiBuffer& midiMessages)
{
    const int numSamples = buffer.getNumSamples();

    if (initialised)
    {
        AudioPlayHead* playHead = getPlayHead();

        if (playHead != 0)
        {
            AudioPlayHead::CurrentPositionInfo position;
            playHead->getCurrentPosition (position);

            vstHostTime.tempo = position.bpm;
            vstHostTime.timeSigNumerator = position.timeSigNumerator;
            vstHostTime.timeSigDenominator = position.timeSigDenominator;
            vstHostTime.ppqPos = position.ppqPosition;
            vstHostTime.barStartPos = position.ppqPositionOfLastBarStart;
            vstHostTime.flags |= kVstTempoValid | kVstTimeSigValid | kVstPpqPosValid | kVstBarsValid;

            if (position.isPlaying)
                vstHostTime.flags |= kVstTransportPlaying;
            else
                vstHostTime.flags &= ~kVstTransportPlaying;
        }

#if JUCE_WIN32
        vstHostTime.nanoSeconds = timeGetTime() * 1000000.0;
#elif JUCE_LINUX
        timeval micro;
        gettimeofday (&micro, 0);
        vstHostTime.nanoSeconds = micro.tv_usec * 1000.0;
#elif JUCE_MAC
        UnsignedWide micro;
        Microseconds (&micro);
        vstHostTime.nanoSeconds = micro.lo * 1000.0;
#endif

        if (wantsMidiMessages)
        {
            midiEventsToSend.clear();
            midiEventsToSend.ensureSize (1);

            MidiBuffer::Iterator iter (midiMessages);
            const uint8* midiData;
            int numBytesOfMidiData, samplePosition;

            while (iter.getNextEvent (midiData, numBytesOfMidiData, samplePosition))
            {
                midiEventsToSend.addEvent (midiData, numBytesOfMidiData,
                                           jlimit (0, numSamples - 1, samplePosition));
            }

            try
            {
                effect->dispatcher (effect, effProcessEvents, 0, 0, midiEventsToSend.events, 0);
            }
            catch (...)
            {}
        }

        int i;
        const int maxChans = jmax (effect->numInputs, effect->numOutputs);

        for (i = 0; i < maxChans; ++i)
            channels[i] = buffer.getSampleData (i);

        channels [maxChans] = 0;

        _clearfp();

        if ((effect->flags & effFlagsCanReplacing) != 0)
        {
            try
            {
                effect->processReplacing (effect, channels, channels, numSamples);
            }
            catch (...)
            {}
        }
        else
        {
            tempBuffer.setSize (effect->numOutputs, numSamples);
            tempBuffer.clear();

            float* outs [64];

            for (i = effect->numOutputs; --i >= 0;)
                outs[i] = tempBuffer.getSampleData (i);

            outs [effect->numOutputs] = 0;

            try
            {
                effect->process (effect, channels, outs, numSamples);
            }
            catch (...)
            {}

            for (i = effect->numOutputs; --i >= 0;)
                buffer.copyFrom (i, 0, outs[i], numSamples);
        }
    }
    else
    {
        // Not initialised, so just bypass..
        for (int i = getNumInputChannels(); i < getNumOutputChannels(); ++i)
            buffer.clear (i, 0, buffer.getNumSamples());
    }

    {
        // copy any incoming midi..
        const ScopedLock sl (midiInLock);

        midiMessages = incomingMidi;
        incomingMidi.clear();
    }
}

//==============================================================================
void VSTPluginInstance::handleMidiFromPlugin (const VstEvents* const events)
{
    if (events != 0)
    {
        const ScopedLock sl (midiInLock);
        VSTMidiEventList::addEventsToMidiBuffer (events, incomingMidi);
    }
}

//==============================================================================
static Array <VSTPluginWindow*> activeVSTWindows;

//==============================================================================
class VSTPluginWindow   : public AudioProcessorEditor,
                          #if ! JUCE_MAC
                          public ComponentMovementWatcher,
                          #endif
                          public Timer
{
public:
    //==============================================================================
    VSTPluginWindow (VSTPluginInstance& plugin_)
        : AudioProcessorEditor (&plugin_),
#if ! JUCE_MAC
          ComponentMovementWatcher (this),
#endif
          plugin (plugin_),
          isOpen (false),
          wasShowing (false),
          pluginRefusesToResize (false),
          pluginWantsKeys (false),
          alreadyInside (false),
          recursiveResize (false)
    {
#if JUCE_WIN32
        sizeCheckCount = 0;
        pluginHWND = 0;
#elif JUCE_LINUX
        pluginWindow = None;
        pluginProc = None;
#else
        addAndMakeVisible (innerWrapper = new InnerWrapperComponent (this));
#endif

        activeVSTWindows.add (this);

        setSize (1, 1);
        setOpaque (true);
        setVisible (true);
    }

    ~VSTPluginWindow()
    {
#if JUCE_MAC
        innerWrapper = 0;
#else
        closePluginWindow();
#endif
        activeVSTWindows.removeValue (this);
        plugin.editorBeingDeleted (this);
    }

    //==============================================================================
#if ! JUCE_MAC
    void componentMovedOrResized (bool /*wasMoved*/, bool /*wasResized*/)
    {
        if (recursiveResize)
            return;

        Component* const topComp = getTopLevelComponent();

        if (topComp->getPeer() != 0)
        {
            int x = 0, y = 0;
            relativePositionToOtherComponent (topComp, x, y);

            recursiveResize = true;

#if JUCE_WIN32
            if (pluginHWND != 0)
                MoveWindow (pluginHWND, x, y, getWidth(), getHeight(), TRUE);
#elif JUCE_LINUX
            if (pluginWindow != 0)
            {
                XResizeWindow (display, pluginWindow, getWidth(), getHeight());
                XMoveWindow (display, pluginWindow, x, y);
                XMapRaised (display, pluginWindow);
            }
#endif

            recursiveResize = false;
        }
    }

    void componentVisibilityChanged (Component&)
    {
        const bool isShowingNow = isShowing();

        if (wasShowing != isShowingNow)
        {
            wasShowing = isShowingNow;

            if (isShowingNow)
                openPluginWindow();
            else
                closePluginWindow();
        }

        componentMovedOrResized (true, true);
    }

    void componentPeerChanged()
    {
        closePluginWindow();
        openPluginWindow();
    }
#endif

    //==============================================================================
    bool keyStateChanged (const bool)
    {
        return pluginWantsKeys;
    }

    bool keyPressed (const KeyPress&)
    {
        return pluginWantsKeys;
    }

    //==============================================================================
#if JUCE_MAC
    void paint (Graphics& g)
    {
        g.fillAll (Colours::black);
    }
#else
    void paint (Graphics& g)
    {
        if (isOpen)
        {
            ComponentPeer* const peer = getPeer();

            if (peer != 0)
            {
                peer->addMaskedRegion (getScreenX() - peer->getScreenX(),
                                       getScreenY() - peer->getScreenY(),
                                       getWidth(), getHeight());

#if JUCE_LINUX
                if (pluginWindow != 0)
                {
                    const Rectangle clip (g.getClipBounds());

                    XEvent ev;
                    zerostruct (ev);
                    ev.xexpose.type = Expose;
                    ev.xexpose.display = display;
                    ev.xexpose.window = pluginWindow;
                    ev.xexpose.x = clip.getX();
                    ev.xexpose.y = clip.getY();
                    ev.xexpose.width = clip.getWidth();
                    ev.xexpose.height = clip.getHeight();

                    sendEventToChild (&ev);
                }
#endif
            }
        }
        else
        {
            g.fillAll (Colours::black);
        }
    }
#endif

    //==============================================================================
    void timerCallback()
    {
#if JUCE_WIN32
        if (--sizeCheckCount <= 0)
        {
            sizeCheckCount = 10;

            checkPluginWindowSize();
        }
#endif

        try
        {
            static bool reentrant = false;

            if (! reentrant)
            {
                reentrant = true;
                plugin.dispatch (effEditIdle, 0, 0, 0, 0);
                reentrant = false;
            }
        }
        catch (...)
        {}
    }

    //==============================================================================
    void mouseDown (const MouseEvent& e)
    {
#if JUCE_LINUX
        if (pluginWindow == 0)
            return;

        toFront (true);

        XEvent ev;
        zerostruct (ev);
        ev.xbutton.display = display;
        ev.xbutton.type = ButtonPress;
        ev.xbutton.window = pluginWindow;
        ev.xbutton.root = RootWindow (display, DefaultScreen (display));
        ev.xbutton.time = CurrentTime;
        ev.xbutton.x = e.x;
        ev.xbutton.y = e.y;
        ev.xbutton.x_root = e.getScreenX();
        ev.xbutton.y_root = e.getScreenY();

        translateJuceToXButtonModifiers (e, ev);

        sendEventToChild (&ev);

#elif JUCE_WIN32
        (void) e;

        toFront (true);
#endif
    }

    void broughtToFront()
    {
        activeVSTWindows.removeValue (this);
        activeVSTWindows.add (this);

#if JUCE_MAC
        dispatch (effEditTop, 0, 0, 0, 0);
#endif
    }

    //==============================================================================
    juce_UseDebuggingNewOperator

private:
    VSTPluginInstance& plugin;
    bool isOpen, wasShowing, recursiveResize;
    bool pluginWantsKeys, pluginRefusesToResize, alreadyInside;

#if JUCE_WIN32
    HWND pluginHWND;
    void* originalWndProc;
    int sizeCheckCount;
#elif JUCE_LINUX
    Window pluginWindow;
    EventProcPtr pluginProc;
#endif

    //==============================================================================
#if JUCE_MAC
    void openPluginWindow (WindowRef parentWindow)
    {
        if (isOpen || parentWindow == 0)
            return;

        isOpen = true;

        ERect* rect = 0;
        dispatch (effEditGetRect, 0, 0, &rect, 0);
        dispatch (effEditOpen, 0, 0, parentWindow, 0);

        // do this before and after like in the steinberg example
        dispatch (effEditGetRect, 0, 0, &rect, 0);
        dispatch (effGetProgram, 0, 0, 0, 0); // also in steinberg code

        // Install keyboard hooks
        pluginWantsKeys = (dispatch (effKeysRequired, 0, 0, 0, 0) == 0);

        // double-check it's not too tiny
        int w = 250, h = 150;

        if (rect != 0)
        {
            w = rect->right - rect->left;
            h = rect->bottom - rect->top;

            if (w == 0 || h == 0)
            {
                w = 250;
                h = 150;
            }
        }

        w = jmax (w, 32);
        h = jmax (h, 32);

        setSize (w, h);

        startTimer (18 + JUCE_NAMESPACE::Random::getSystemRandom().nextInt (5));
        repaint();
    }

#else
    void openPluginWindow()
    {
        if (isOpen || getWindowHandle() == 0)
            return;

        log (T("Opening VST UI: ") + plugin.name);
        isOpen = true;

        ERect* rect = 0;
        dispatch (effEditGetRect, 0, 0, &rect, 0);
        dispatch (effEditOpen, 0, 0, getWindowHandle(), 0);

        // do this before and after like in the steinberg example
        dispatch (effEditGetRect, 0, 0, &rect, 0);
        dispatch (effGetProgram, 0, 0, 0, 0); // also in steinberg code

        // Install keyboard hooks
        pluginWantsKeys = (dispatch (effKeysRequired, 0, 0, 0, 0) == 0);

#if JUCE_WIN32
        originalWndProc = 0;
        pluginHWND = GetWindow ((HWND) getWindowHandle(), GW_CHILD);

        if (pluginHWND == 0)
        {
            isOpen = false;
            setSize (300, 150);
            return;
        }

        #pragma warning (push)
        #pragma warning (disable: 4244)

        originalWndProc = (void*) GetWindowLongPtr (pluginHWND, GWL_WNDPROC);

        if (! pluginWantsKeys)
            SetWindowLongPtr (pluginHWND, GWL_WNDPROC, (LONG_PTR) vstHookWndProc);

        #pragma warning (pop)

        int w, h;
        RECT r;
        GetWindowRect (pluginHWND, &r);
        w = r.right - r.left;
        h = r.bottom - r.top;

        if (rect != 0)
        {
            const int rw = rect->right - rect->left;
            const int rh = rect->bottom - rect->top;

            if ((rw > 50 && rh > 50 && rw < 2000 && rh < 2000 && rw != w && rh != h)
                || ((w == 0 && rw > 0) || (h == 0 && rh > 0)))
            {
                // very dodgy logic to decide which size is right.
                if (abs (rw - w) > 350 || abs (rh - h) > 350)
                {
                    SetWindowPos (pluginHWND, 0,
                                  0, 0, rw, rh,
                                  SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

                    GetWindowRect (pluginHWND, &r);

                    w = r.right - r.left;
                    h = r.bottom - r.top;

                    pluginRefusesToResize = (w != rw) || (h != rh);

                    w = rw;
                    h = rh;
                }
            }
        }

#elif JUCE_LINUX
        pluginWindow = getChildWindow ((Window) getWindowHandle());

        if (pluginWindow != 0)
            pluginProc = (EventProcPtr) getPropertyFromXWindow (pluginWindow,
                                                                XInternAtom (display, "_XEventProc", False));

        int w = 250, h = 150;

        if (rect != 0)
        {
            w = rect->right - rect->left;
            h = rect->bottom - rect->top;

            if (w == 0 || h == 0)
            {
                w = 250;
                h = 150;
            }
        }

        if (pluginWindow != 0)
            XMapRaised (display, pluginWindow);
#endif

        // double-check it's not too tiny
        w = jmax (w, 32);
        h = jmax (h, 32);

        setSize (w, h);

#if JUCE_WIN32
        checkPluginWindowSize();
#endif

        startTimer (18 + JUCE_NAMESPACE::Random::getSystemRandom().nextInt (5));
        repaint();
    }
#endif

    //==============================================================================
#if ! JUCE_MAC
    void closePluginWindow()
    {
        if (isOpen)
        {
            log (T("Closing VST UI: ") + plugin.getName());
            isOpen = false;

            dispatch (effEditClose, 0, 0, 0, 0);

#if JUCE_WIN32
            #pragma warning (push)
            #pragma warning (disable: 4244)

            if (pluginHWND != 0 && IsWindow (pluginHWND))
                SetWindowLongPtr (pluginHWND, GWL_WNDPROC, (LONG_PTR) originalWndProc);

            #pragma warning (pop)

            stopTimer();

            if (pluginHWND != 0 && IsWindow (pluginHWND))
                DestroyWindow (pluginHWND);

            pluginHWND = 0;
#elif JUCE_LINUX
            stopTimer();
            pluginWindow = 0;
            pluginProc = 0;
#endif
        }
    }
#endif

    //==============================================================================
    int dispatch (const int opcode, const int index, const int value, void* const ptr, float opt)
    {
        return plugin.dispatch (opcode, index, value, ptr, opt);
    }

    //==============================================================================
#if JUCE_WIN32
    void checkPluginWindowSize() throw()
    {
        RECT r;
        GetWindowRect (pluginHWND, &r);
        const int w = r.right - r.left;
        const int h = r.bottom - r.top;

        if (isShowing() && w > 0 && h > 0
             && (w != getWidth() || h != getHeight())
             && ! pluginRefusesToResize)
        {
            setSize (w, h);
            sizeCheckCount = 0;
        }
    }

    // hooks to get keyboard events from VST windows..
    static LRESULT CALLBACK vstHookWndProc (HWND hW, UINT message, WPARAM wParam, LPARAM lParam)
    {
        for (int i = activeVSTWindows.size(); --i >= 0;)
        {
            const VSTPluginWindow* const w = (const VSTPluginWindow*) activeVSTWindows.getUnchecked (i);

            if (w->pluginHWND == hW)
            {
                if (message == WM_CHAR
                    || message == WM_KEYDOWN
                    || message == WM_SYSKEYDOWN
                    || message == WM_KEYUP
                    || message == WM_SYSKEYUP
                    || message == WM_APPCOMMAND)
                {
                    SendMessage ((HWND) w->getTopLevelComponent()->getWindowHandle(),
                                 message, wParam, lParam);
                }

                return CallWindowProc ((WNDPROC) (w->originalWndProc),
                                       (HWND) w->pluginHWND,
                                       message,
                                       wParam,
                                       lParam);
            }
        }

        return DefWindowProc (hW, message, wParam, lParam);
    }
#endif

#if JUCE_LINUX
    //==============================================================================
    // overload mouse/keyboard events to forward them to the plugin's inner window..
    void sendEventToChild (XEvent* event)
    {
        if (pluginProc != 0)
        {
            // if the plugin publishes an event procedure, pass the event directly..
            pluginProc (event);
        }
        else if (pluginWindow != 0)
        {
            // if the plugin has a window, then send the event to the window so that
            // its message thread will pick it up..
            XSendEvent (display, pluginWindow, False, 0L, event);
            XFlush (display);
        }
    }

    void mouseEnter (const MouseEvent& e)
    {
        if (pluginWindow != 0)
        {
            XEvent ev;
            zerostruct (ev);
            ev.xcrossing.display = display;
            ev.xcrossing.type = EnterNotify;
            ev.xcrossing.window = pluginWindow;
            ev.xcrossing.root = RootWindow (display, DefaultScreen (display));
            ev.xcrossing.time = CurrentTime;
            ev.xcrossing.x = e.x;
            ev.xcrossing.y = e.y;
            ev.xcrossing.x_root = e.getScreenX();
            ev.xcrossing.y_root = e.getScreenY();
            ev.xcrossing.mode = NotifyNormal; // NotifyGrab, NotifyUngrab
            ev.xcrossing.detail = NotifyAncestor; // NotifyVirtual, NotifyInferior, NotifyNonlinear,NotifyNonlinearVirtual

            translateJuceToXCrossingModifiers (e, ev);

            sendEventToChild (&ev);
        }
    }

    void mouseExit (const MouseEvent& e)
    {
        if (pluginWindow != 0)
        {
            XEvent ev;
            zerostruct (ev);
            ev.xcrossing.display = display;
            ev.xcrossing.type = LeaveNotify;
            ev.xcrossing.window = pluginWindow;
            ev.xcrossing.root = RootWindow (display, DefaultScreen (display));
            ev.xcrossing.time = CurrentTime;
            ev.xcrossing.x = e.x;
            ev.xcrossing.y = e.y;
            ev.xcrossing.x_root = e.getScreenX();
            ev.xcrossing.y_root = e.getScreenY();
            ev.xcrossing.mode = NotifyNormal; // NotifyGrab, NotifyUngrab
            ev.xcrossing.detail = NotifyAncestor; // NotifyVirtual, NotifyInferior, NotifyNonlinear,NotifyNonlinearVirtual
            ev.xcrossing.focus = hasKeyboardFocus (true); // TODO - yes ?

            translateJuceToXCrossingModifiers (e, ev);

            sendEventToChild (&ev);
        }
    }

    void mouseMove (const MouseEvent& e)
    {
        if (pluginWindow != 0)
        {
            XEvent ev;
            zerostruct (ev);
            ev.xmotion.display = display;
            ev.xmotion.type = MotionNotify;
            ev.xmotion.window = pluginWindow;
            ev.xmotion.root = RootWindow (display, DefaultScreen (display));
            ev.xmotion.time = CurrentTime;
            ev.xmotion.is_hint = NotifyNormal;
            ev.xmotion.x = e.x;
            ev.xmotion.y = e.y;
            ev.xmotion.x_root = e.getScreenX();
            ev.xmotion.y_root = e.getScreenY();

            sendEventToChild (&ev);
        }
    }

    void mouseDrag (const MouseEvent& e)
    {
        if (pluginWindow != 0)
        {
            XEvent ev;
            zerostruct (ev);
            ev.xmotion.display = display;
            ev.xmotion.type = MotionNotify;
            ev.xmotion.window = pluginWindow;
            ev.xmotion.root = RootWindow (display, DefaultScreen (display));
            ev.xmotion.time = CurrentTime;
            ev.xmotion.x = e.x ;
            ev.xmotion.y = e.y;
            ev.xmotion.x_root = e.getScreenX();
            ev.xmotion.y_root = e.getScreenY();
            ev.xmotion.is_hint = NotifyNormal;

            translateJuceToXMotionModifiers (e, ev);
            sendEventToChild (&ev);
        }
    }

    void mouseUp (const MouseEvent& e)
    {
        if (pluginWindow != 0)
        {
            XEvent ev;
            zerostruct (ev);
            ev.xbutton.display = display;
            ev.xbutton.type = ButtonRelease;
            ev.xbutton.window = pluginWindow;
            ev.xbutton.root = RootWindow (display, DefaultScreen (display));
            ev.xbutton.time = CurrentTime;
            ev.xbutton.x = e.x;
            ev.xbutton.y = e.y;
            ev.xbutton.x_root = e.getScreenX();
            ev.xbutton.y_root = e.getScreenY();

            translateJuceToXButtonModifiers (e, ev);
            sendEventToChild (&ev);
        }
    }

    void mouseWheelMove (const MouseEvent& e,
                         float incrementX,
                         float incrementY)
    {
        if (pluginWindow != 0)
        {
            XEvent ev;
            zerostruct (ev);
            ev.xbutton.display = display;
            ev.xbutton.type = ButtonPress;
            ev.xbutton.window = pluginWindow;
            ev.xbutton.root = RootWindow (display, DefaultScreen (display));
            ev.xbutton.time = CurrentTime;
            ev.xbutton.x = e.x;
            ev.xbutton.y = e.y;
            ev.xbutton.x_root = e.getScreenX();
            ev.xbutton.y_root = e.getScreenY();

            translateJuceToXMouseWheelModifiers (e, incrementY, ev);
            sendEventToChild (&ev);

            // TODO - put a usleep here ?

            ev.xbutton.type = ButtonRelease;
            sendEventToChild (&ev);
        }
    }
#endif

#if JUCE_MAC

#if !JUCE_SUPPORT_CARBON
 #error "To build VSTs, you need to enable the JUCE_SUPPORT_CARBON flag in your config!"
#endif

    class InnerWrapperComponent   : public CarbonViewWrapperComponent
    {
    public:
        InnerWrapperComponent (VSTPluginWindow* const owner_)
            : owner (owner_),
              alreadyInside (false)
        {
        }

        ~InnerWrapperComponent()
        {
            deleteWindow();
        }

        HIViewRef attachView (WindowRef windowRef, HIViewRef rootView)
        {
            owner->openPluginWindow (windowRef);
            return 0;
        }

        void removeView (HIViewRef)
        {
            owner->dispatch (effEditClose, 0, 0, 0, 0);
            owner->dispatch (effEditSleep, 0, 0, 0, 0);
        }

        bool getEmbeddedViewSize (int& w, int& h)
        {
            ERect* rect = 0;
            owner->dispatch (effEditGetRect, 0, 0, &rect, 0);
            w = rect->right - rect->left;
            h = rect->bottom - rect->top;
            return true;
        }

        void mouseDown (int x, int y)
        {
            if (! alreadyInside)
            {
                alreadyInside = true;
                getTopLevelComponent()->toFront (true);
                owner->dispatch (effEditMouse, x, y, 0, 0);
                alreadyInside = false;
            }
            else
            {
                PostEvent (::mouseDown, 0);
            }
        }

        void paint()
        {
            ComponentPeer* const peer = getPeer();

            if (peer != 0)
            {
                ERect r;
                r.left = getScreenX() - peer->getScreenX();
                r.right = r.left + getWidth();
                r.top = getScreenY() - peer->getScreenY();
                r.bottom = r.top + getHeight();

                owner->dispatch (effEditDraw, 0, 0, &r, 0);
            }
        }

    private:
        VSTPluginWindow* const owner;
        bool alreadyInside;
    };

    friend class InnerWrapperComponent;
    ScopedPointer <InnerWrapperComponent> innerWrapper;

    void resized()
    {
        innerWrapper->setSize (getWidth(), getHeight());
    }
#endif
};

//==============================================================================
AudioProcessorEditor* VSTPluginInstance::createEditor()
{
    if (hasEditor())
        return new VSTPluginWindow (*this);

    return 0;
}


//==============================================================================
void VSTPluginInstance::handleAsyncUpdate()
{
    // indicates that something about the plugin has changed..
    updateHostDisplay();
}

//==============================================================================
bool VSTPluginInstance::restoreProgramSettings (const fxProgram* const prog)
{
    if (vst_swap (prog->chunkMagic) == 'CcnK' && vst_swap (prog->fxMagic) == 'FxCk')
    {
        changeProgramName (getCurrentProgram(), prog->prgName);

        for (int i = 0; i < vst_swap (prog->numParams); ++i)
            setParameter (i, vst_swapFloat (prog->params[i]));

        return true;
    }

    return false;
}

bool VSTPluginInstance::loadFromFXBFile (const void* const data,
                                         const int dataSize)
{
    if (dataSize < 28)
        return false;

    const fxSet* const set = (const fxSet*) data;

    if ((vst_swap (set->chunkMagic) != 'CcnK' && vst_swap (set->chunkMagic) != 'KncC')
         || vst_swap (set->version) > fxbVersionNum)
        return false;

    if (vst_swap (set->fxMagic) == 'FxBk')
    {
        // bank of programs
        if (vst_swap (set->numPrograms) >= 0)
        {
            const int oldProg = getCurrentProgram();
            const int numParams = vst_swap (((const fxProgram*) (set->programs))->numParams);
            const int progLen = sizeof (fxProgram) + (numParams - 1) * sizeof (float);

            for (int i = 0; i < vst_swap (set->numPrograms); ++i)
            {
                if (i != oldProg)
                {
                    const fxProgram* const prog = (const fxProgram*) (((const char*) (set->programs)) + i * progLen);
                    if (((const char*) prog) - ((const char*) set) >= dataSize)
                        return false;

                    if (vst_swap (set->numPrograms) > 0)
                        setCurrentProgram (i);

                    if (! restoreProgramSettings (prog))
                        return false;
                }
            }

            if (vst_swap (set->numPrograms) > 0)
                setCurrentProgram (oldProg);

            const fxProgram* const prog = (const fxProgram*) (((const char*) (set->programs)) + oldProg * progLen);
            if (((const char*) prog) - ((const char*) set) >= dataSize)
                return false;

            if (! restoreProgramSettings (prog))
                return false;
        }
    }
    else if (vst_swap (set->fxMagic) == 'FxCk')
    {
        // single program
        const fxProgram* const prog = (const fxProgram*) data;

        if (vst_swap (prog->chunkMagic) != 'CcnK')
            return false;

        changeProgramName (getCurrentProgram(), prog->prgName);

        for (int i = 0; i < vst_swap (prog->numParams); ++i)
            setParameter (i, vst_swapFloat (prog->params[i]));
    }
    else if (vst_swap (set->fxMagic) == 'FBCh' || vst_swap (set->fxMagic) == 'hCBF')
    {
        // non-preset chunk
        const fxChunkSet* const cset = (const fxChunkSet*) data;

        if (vst_swap (cset->chunkSize) + sizeof (fxChunkSet) - 8 > (unsigned int) dataSize)
            return false;

        setChunkData (cset->chunk, vst_swap (cset->chunkSize), false);
    }
    else if (vst_swap (set->fxMagic) == 'FPCh' || vst_swap (set->fxMagic) == 'hCPF')
    {
        // preset chunk
        const fxProgramSet* const cset = (const fxProgramSet*) data;

        if (vst_swap (cset->chunkSize) + sizeof (fxProgramSet) - 8 > (unsigned int) dataSize)
            return false;

        setChunkData (cset->chunk, vst_swap (cset->chunkSize), true);

        changeProgramName (getCurrentProgram(), cset->name);
    }
    else
    {
        return false;
    }

    return true;
}

//==============================================================================
void VSTPluginInstance::setParamsInProgramBlock (fxProgram* const prog) throw()
{
    const int numParams = getNumParameters();

    prog->chunkMagic = vst_swap ('CcnK');
    prog->byteSize = 0;
    prog->fxMagic = vst_swap ('FxCk');
    prog->version = vst_swap (fxbVersionNum);
    prog->fxID = vst_swap (getUID());
    prog->fxVersion = vst_swap (getVersionNumber());
    prog->numParams = vst_swap (numParams);

    getCurrentProgramName().copyToBuffer (prog->prgName, sizeof (prog->prgName) - 1);

    for (int i = 0; i < numParams; ++i)
        prog->params[i] = vst_swapFloat (getParameter (i));
}

bool VSTPluginInstance::saveToFXBFile (MemoryBlock& dest, bool isFXB, int maxSizeMB)
{
    const int numPrograms = getNumPrograms();
    const int numParams = getNumParameters();

    if (usesChunks())
    {
        if (isFXB)
        {
            MemoryBlock chunk;
            getChunkData (chunk, false, maxSizeMB);

            const int totalLen = sizeof (fxChunkSet) + chunk.getSize() - 8;
            dest.setSize (totalLen, true);

            fxChunkSet* const set = (fxChunkSet*) dest.getData();
            set->chunkMagic = vst_swap ('CcnK');
            set->byteSize = 0;
            set->fxMagic = vst_swap ('FBCh');
            set->version = vst_swap (fxbVersionNum);
            set->fxID = vst_swap (getUID());
            set->fxVersion = vst_swap (getVersionNumber());
            set->numPrograms = vst_swap (numPrograms);
            set->chunkSize = vst_swap (chunk.getSize());

            chunk.copyTo (set->chunk, 0, chunk.getSize());
        }
        else
        {
            MemoryBlock chunk;
            getChunkData (chunk, true, maxSizeMB);

            const int totalLen = sizeof (fxProgramSet) + chunk.getSize() - 8;
            dest.setSize (totalLen, true);

            fxProgramSet* const set = (fxProgramSet*) dest.getData();
            set->chunkMagic = vst_swap ('CcnK');
            set->byteSize = 0;
            set->fxMagic = vst_swap ('FPCh');
            set->version = vst_swap (fxbVersionNum);
            set->fxID = vst_swap (getUID());
            set->fxVersion = vst_swap (getVersionNumber());
            set->numPrograms = vst_swap (numPrograms);
            set->chunkSize = vst_swap (chunk.getSize());

            getCurrentProgramName().copyToBuffer (set->name, sizeof (set->name) - 1);
            chunk.copyTo (set->chunk, 0, chunk.getSize());
        }
    }
    else
    {
        if (isFXB)
        {
            const int progLen = sizeof (fxProgram) + (numParams - 1) * sizeof (float);
            const int len = (sizeof (fxSet) - sizeof (fxProgram)) + progLen * jmax (1, numPrograms);
            dest.setSize (len, true);

            fxSet* const set = (fxSet*) dest.getData();
            set->chunkMagic = vst_swap ('CcnK');
            set->byteSize = 0;
            set->fxMagic = vst_swap ('FxBk');
            set->version = vst_swap (fxbVersionNum);
            set->fxID = vst_swap (getUID());
            set->fxVersion = vst_swap (getVersionNumber());
            set->numPrograms = vst_swap (numPrograms);

            const int oldProgram = getCurrentProgram();
            MemoryBlock oldSettings;
            createTempParameterStore (oldSettings);

            setParamsInProgramBlock ((fxProgram*) (((char*) (set->programs)) + oldProgram * progLen));

            for (int i = 0; i < numPrograms; ++i)
            {
                if (i != oldProgram)
                {
                    setCurrentProgram (i);
                    setParamsInProgramBlock ((fxProgram*) (((char*) (set->programs)) + i * progLen));
                }
            }

            setCurrentProgram (oldProgram);
            restoreFromTempParameterStore (oldSettings);
        }
        else
        {
            const int totalLen = sizeof (fxProgram) + (numParams - 1) * sizeof (float);
            dest.setSize (totalLen, true);

            setParamsInProgramBlock ((fxProgram*) dest.getData());
        }
    }

    return true;
}

void VSTPluginInstance::getChunkData (MemoryBlock& mb, bool isPreset, int maxSizeMB) const
{
    if (usesChunks())
    {
        void* data = 0;
        const int bytes = dispatch (effGetChunk, isPreset ? 1 : 0, 0, &data, 0.0f);

        if (data != 0 && bytes <= maxSizeMB * 1024 * 1024)
        {
            mb.setSize (bytes);
            mb.copyFrom (data, 0, bytes);
        }
    }
}

void VSTPluginInstance::setChunkData (const char* data, int size, bool isPreset)
{
    if (size > 0 && usesChunks())
    {
        dispatch (effSetChunk, isPreset ? 1 : 0, size, (void*) data, 0.0f);

        if (! isPreset)
            updateStoredProgramNames();
    }
}

//==============================================================================
void VSTPluginInstance::timerCallback()
{
    if (dispatch (effIdle, 0, 0, 0, 0) == 0)
        stopTimer();
}

int VSTPluginInstance::dispatch (const int opcode, const int index, const int value, void* const ptr, float opt) const
{
    const ScopedLock sl (lock);

    ++insideVSTCallback;
    int result = 0;

    try
    {
        if (effect != 0)
        {
#if JUCE_MAC
            if (module->resFileId != 0)
                UseResFile (module->resFileId);

            CGrafPtr oldPort;

            if (getActiveEditor() != 0)
            {
                int x = 0, y = 0;
                getActiveEditor()->relativePositionToOtherComponent (getActiveEditor()->getTopLevelComponent(), x, y);

                GetPort (&oldPort);
                SetPortWindowPort ((WindowRef) getActiveEditor()->getWindowHandle());
                SetOrigin (-x, -y);
            }
#endif

            result = effect->dispatcher (effect, opcode, index, value, ptr, opt);

#if JUCE_MAC
            if (getActiveEditor() != 0)
                SetPort (oldPort);

            module->resFileId = CurResFile();
#endif

            --insideVSTCallback;
            return result;
        }
    }
    catch (...)
    {
        //char s[512];
        //sprintf (s, "dispatcher (%d, %d, %d, %x, %f)", opcode, index, value, (int)ptr, opt);
    }

    --insideVSTCallback;
    return result;
}

//==============================================================================
// handles non plugin-specific callbacks..

static const int defaultVSTSampleRateValue = 16384;
static const int defaultVSTBlockSizeValue = 512;


static VstIntPtr handleGeneralCallback (VstInt32 opcode, VstInt32 index, VstInt32 value, void *ptr, float opt)
{
    (void) index;
    (void) value;
    (void) opt;

    switch (opcode)
    {
    case audioMasterCanDo:
        {
            static const char* canDos[] = { "supplyIdle",
                                            "sendVstEvents",
                                            "sendVstMidiEvent",
                                            "sendVstTimeInfo",
                                            "receiveVstEvents",
                                            "receiveVstMidiEvent",
                                            "supportShell",
                                            "shellCategory" };

            for (int i = 0; i < numElementsInArray (canDos); ++i)
                if (strcmp (canDos[i], (const char*) ptr) == 0)
                    return 1;

            return 0;
        }

    case audioMasterVersion:
        return 0x2400;
    case audioMasterCurrentId:
        return shellUIDToCreate;
    case audioMasterGetNumAutomatableParameters:
        return 0;
    case audioMasterGetAutomationState:
        return 1;

    case audioMasterGetVendorVersion:
        return 0x0101;
    case audioMasterGetVendorString:
    case audioMasterGetProductString:
        {
            String hostName ("Juce VST Host");

            if (JUCEApplication::getInstance() != 0)
                hostName = JUCEApplication::getInstance()->getApplicationName();

            hostName.copyToBuffer ((char*) ptr, jmin (kVstMaxVendorStrLen, kVstMaxProductStrLen) - 1);
        }
        break;

    case audioMasterGetSampleRate:
        return (VstIntPtr) defaultVSTSampleRateValue;

    case audioMasterGetBlockSize:
        return (VstIntPtr) defaultVSTBlockSizeValue;

    case audioMasterSetOutputSampleRate:
        return 0;

    default:
        DBG ("*** Unhandled VST Callback: " + String ((int) opcode));
        break;
    }

    return 0;
}

// handles callbacks for a specific plugin
VstIntPtr VSTPluginInstance::handleCallback (VstInt32 opcode, VstInt32 index, VstInt32 value, void *ptr, float opt)
{
    switch (opcode)
    {
    case audioMasterAutomate:
        sendParamChangeMessageToListeners (index, opt);
        break;

    case audioMasterProcessEvents:
        handleMidiFromPlugin ((const VstEvents*) ptr);
        break;

    case audioMasterGetTime:
        #ifdef _MSC_VER
         #pragma warning (push)
         #pragma warning (disable: 4311)
        #endif

        return (VstIntPtr) &vstHostTime;

        #ifdef _MSC_VER
         #pragma warning (pop)
        #endif
        break;

    case audioMasterIdle:
        if (insideVSTCallback == 0 && MessageManager::getInstance()->isThisTheMessageThread())
        {
            ++insideVSTCallback;
#if JUCE_MAC
            if (getActiveEditor() != 0)
                dispatch (effEditIdle, 0, 0, 0, 0);
#endif
            juce_callAnyTimersSynchronously();

            handleUpdateNowIfNeeded();

            for (int i = ComponentPeer::getNumPeers(); --i >= 0;)
                ComponentPeer::getPeer (i)->performAnyPendingRepaintsNow();

            --insideVSTCallback;
        }
        break;

    case audioMasterUpdateDisplay:
        triggerAsyncUpdate();
        break;

    case audioMasterTempoAt:
        // returns (10000 * bpm)
        break;

    case audioMasterNeedIdle:
        startTimer (50);
        break;

    case audioMasterSizeWindow:
        if (getActiveEditor() != 0)
            getActiveEditor()->setSize (index, value);

        return 1;

    case audioMasterGetSampleRate:
        return (VstIntPtr) (getSampleRate() > 0 ? getSampleRate() : defaultVSTSampleRateValue);

    case audioMasterGetBlockSize:
        return (VstIntPtr) (getBlockSize() > 0 ? getBlockSize() : defaultVSTBlockSizeValue);

    case audioMasterWantMidi:
        wantsMidiMessages = true;
        break;

    case audioMasterGetDirectory:
      #if JUCE_MAC
        return (VstIntPtr) (void*) &module->parentDirFSSpec;
      #else
        return (VstIntPtr) (pointer_sized_uint) (const char*) module->fullParentDirectoryPathName;
      #endif

    case audioMasterGetAutomationState:
        // returns 0: not supported, 1: off, 2:read, 3:write, 4:read/write
        break;

    // none of these are handled (yet)..
    case audioMasterBeginEdit:
    case audioMasterEndEdit:
    case audioMasterSetTime:
    case audioMasterPinConnected:
    case audioMasterGetParameterQuantization:
    case audioMasterIOChanged:
    case audioMasterGetInputLatency:
    case audioMasterGetOutputLatency:
    case audioMasterGetPreviousPlug:
    case audioMasterGetNextPlug:
    case audioMasterWillReplaceOrAccumulate:
    case audioMasterGetCurrentProcessLevel:
    case audioMasterOfflineStart:
    case audioMasterOfflineRead:
    case audioMasterOfflineWrite:
    case audioMasterOfflineGetCurrentPass:
    case audioMasterOfflineGetCurrentMetaPass:
    case audioMasterVendorSpecific:
    case audioMasterSetIcon:
    case audioMasterGetLanguage:
    case audioMasterOpenWindow:
    case audioMasterCloseWindow:
        break;

    default:
        return handleGeneralCallback (opcode, index, value, ptr, opt);
    }

    return 0;
}

// entry point for all callbacks from the plugin
static VstIntPtr VSTCALLBACK audioMaster (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    try
    {
        if (effect != 0 && effect->resvd2 != 0)
        {
            return ((VSTPluginInstance*)(effect->resvd2))
                        ->handleCallback (opcode, index, value, ptr, opt);
        }

        return handleGeneralCallback (opcode, index, value, ptr, opt);
    }
    catch (...)
    {
        return 0;
    }
}

//==============================================================================
const String VSTPluginInstance::getVersion() const throw()
{
    unsigned int v = dispatch (effGetVendorVersion, 0, 0, 0, 0);

    String s;

    if (v == 0 || v == -1)
        v = getVersionNumber();

    if (v != 0)
    {
        int versionBits[4];
        int n = 0;

        while (v != 0)
        {
            versionBits [n++] = (v & 0xff);
            v >>= 8;
        }

        s << 'V';

        while (n > 0)
        {
            s << versionBits [--n];

            if (n > 0)
                s << '.';
        }
    }

    return s;
}

int VSTPluginInstance::getUID() const throw()
{
    int uid = effect != 0 ? effect->uniqueID : 0;

    if (uid == 0)
        uid = module->file.hashCode();

    return uid;
}

const String VSTPluginInstance::getCategory() const throw()
{
    const char* result = 0;

    switch (dispatch (effGetPlugCategory, 0, 0, 0, 0))
    {
    case kPlugCategEffect:
        result = "Effect";
        break;

    case kPlugCategSynth:
        result = "Synth";
        break;

    case kPlugCategAnalysis:
        result = "Anaylsis";
        break;

    case kPlugCategMastering:
        result = "Mastering";
        break;

    case kPlugCategSpacializer:
        result = "Spacial";
        break;

    case kPlugCategRoomFx:
        result = "Reverb";
        break;

    case kPlugSurroundFx:
        result = "Surround";
        break;

    case kPlugCategRestoration:
        result = "Restoration";
        break;

    case kPlugCategGenerator:
        result = "Tone generation";
        break;

    default:
        break;
    }

    return result;
}

//==============================================================================
float VSTPluginInstance::getParameter (int index)
{
    if (effect != 0 && ((unsigned int) index) < (unsigned int) effect->numParams)
    {
        try
        {
            const ScopedLock sl (lock);
            return effect->getParameter (effect, index);
        }
        catch (...)
        {
        }
    }

    return 0.0f;
}

void VSTPluginInstance::setParameter (int index, float newValue)
{
    if (effect != 0 && ((unsigned int) index) < (unsigned int) effect->numParams)
    {
        try
        {
            const ScopedLock sl (lock);

            if (effect->getParameter (effect, index) != newValue)
                effect->setParameter (effect, index, newValue);
        }
        catch (...)
        {
        }
    }
}

const String VSTPluginInstance::getParameterName (int index)
{
    if (effect != 0)
    {
        jassert (index >= 0 && index < effect->numParams);

        char nm [256];
        zerostruct (nm);
        dispatch (effGetParamName, index, 0, nm, 0);
        return String (nm).trim();
    }

    return String::empty;
}

const String VSTPluginInstance::getParameterLabel (int index) const
{
    if (effect != 0)
    {
        jassert (index >= 0 && index < effect->numParams);

        char nm [256];
        zerostruct (nm);
        dispatch (effGetParamLabel, index, 0, nm, 0);
        return String (nm).trim();
    }

    return String::empty;
}

const String VSTPluginInstance::getParameterText (int index)
{
    if (effect != 0)
    {
        jassert (index >= 0 && index < effect->numParams);

        char nm [256];
        zerostruct (nm);
        dispatch (effGetParamDisplay, index, 0, nm, 0);
        return String (nm).trim();
    }

    return String::empty;
}

bool VSTPluginInstance::isParameterAutomatable (int index) const
{
    if (effect != 0)
    {
        jassert (index >= 0 && index < effect->numParams);
        return dispatch (effCanBeAutomated, index, 0, 0, 0) != 0;
    }

    return false;
}

void VSTPluginInstance::createTempParameterStore (MemoryBlock& dest)
{
    dest.setSize (64 + 4 * getNumParameters());
    dest.fillWith (0);

    getCurrentProgramName().copyToBuffer ((char*) dest.getData(), 63);

    float* const p = (float*) (((char*) dest.getData()) + 64);
    for (int i = 0; i < getNumParameters(); ++i)
        p[i] = getParameter(i);
}

void VSTPluginInstance::restoreFromTempParameterStore (const MemoryBlock& m)
{
    changeProgramName (getCurrentProgram(), (const char*) m.getData());

    float* p = (float*) (((char*) m.getData()) + 64);
    for (int i = 0; i < getNumParameters(); ++i)
        setParameter (i, p[i]);
}

//==============================================================================
void VSTPluginInstance::setCurrentProgram (int newIndex)
{
    if (getNumPrograms() > 0 && newIndex != getCurrentProgram())
        dispatch (effSetProgram, 0, jlimit (0, getNumPrograms() - 1, newIndex), 0, 0);
}

const String VSTPluginInstance::getProgramName (int index)
{
    if (index == getCurrentProgram())
    {
        return getCurrentProgramName();
    }
    else if (effect != 0)
    {
        char nm [256];
        zerostruct (nm);

        if (dispatch (effGetProgramNameIndexed,
                      jlimit (0, getNumPrograms(), index),
                      -1, nm, 0) != 0)
        {
            return String (nm).trim();
        }
    }

    return programNames [index];
}

void VSTPluginInstance::changeProgramName (int index, const String& newName)
{
    if (index == getCurrentProgram())
    {
        if (getNumPrograms() > 0 && newName != getCurrentProgramName())
            dispatch (effSetProgramName, 0, 0, (void*) (const char*) newName.substring (0, 24), 0.0f);
    }
    else
    {
        jassertfalse // xxx not implemented!
    }
}

void VSTPluginInstance::updateStoredProgramNames()
{
    if (effect != 0 && getNumPrograms() > 0)
    {
        char nm [256];
        zerostruct (nm);

        // only do this if the plugin can't use indexed names..
        if (dispatch (effGetProgramNameIndexed, 0, -1, nm, 0) == 0)
        {
            const int oldProgram = getCurrentProgram();
            MemoryBlock oldSettings;
            createTempParameterStore (oldSettings);

            for (int i = 0; i < getNumPrograms(); ++i)
            {
                setCurrentProgram (i);
                getCurrentProgramName();  // (this updates the list)
            }

            setCurrentProgram (oldProgram);
            restoreFromTempParameterStore (oldSettings);
        }
    }
}

const String VSTPluginInstance::getCurrentProgramName()
{
    if (effect != 0)
    {
        char nm [256];
        zerostruct (nm);
        dispatch (effGetProgramName, 0, 0, nm, 0);

        const int index = getCurrentProgram();
        if (programNames[index].isEmpty())
        {
            while (programNames.size() < index)
                programNames.add (String::empty);

            programNames.set (index, String (nm).trim());
        }

        return String (nm).trim();
    }

    return String::empty;
}

//==============================================================================
const String VSTPluginInstance::getInputChannelName (const int index) const
{
    if (index >= 0 && index < getNumInputChannels())
    {
        VstPinProperties pinProps;
        if (dispatch (effGetInputProperties, index, 0, &pinProps, 0.0f) != 0)
            return String (pinProps.label, sizeof (pinProps.label));
    }

    return String::empty;
}

bool VSTPluginInstance::isInputChannelStereoPair (int index) const
{
    if (index < 0 || index >= getNumInputChannels())
        return false;

    VstPinProperties pinProps;
    if (dispatch (effGetInputProperties, index, 0, &pinProps, 0.0f) != 0)
        return (pinProps.flags & kVstPinIsStereo) != 0;

    return true;
}

const String VSTPluginInstance::getOutputChannelName (const int index) const
{
    if (index >= 0 && index < getNumOutputChannels())
    {
        VstPinProperties pinProps;
        if (dispatch (effGetOutputProperties, index, 0, &pinProps, 0.0f) != 0)
            return String (pinProps.label, sizeof (pinProps.label));
    }

    return String::empty;
}

bool VSTPluginInstance::isOutputChannelStereoPair (int index) const
{
    if (index < 0 || index >= getNumOutputChannels())
        return false;

    VstPinProperties pinProps;
    if (dispatch (effGetOutputProperties, index, 0, &pinProps, 0.0f) != 0)
        return (pinProps.flags & kVstPinIsStereo) != 0;

    return true;
}

//==============================================================================
void VSTPluginInstance::setPower (const bool on)
{
    dispatch (effMainsChanged, 0, on ? 1 : 0, 0, 0);
    isPowerOn = on;
}

//==============================================================================
const int defaultMaxSizeMB = 64;

void VSTPluginInstance::getStateInformation (MemoryBlock& destData)
{
    saveToFXBFile (destData, true, defaultMaxSizeMB);
}

void VSTPluginInstance::getCurrentProgramStateInformation (MemoryBlock& destData)
{
    saveToFXBFile (destData, false, defaultMaxSizeMB);
}

void VSTPluginInstance::setStateInformation (const void* data, int sizeInBytes)
{
    loadFromFXBFile (data, sizeInBytes);
}

void VSTPluginInstance::setCurrentProgramStateInformation (const void* data, int sizeInBytes)
{
    loadFromFXBFile (data, sizeInBytes);
}

//==============================================================================
//==============================================================================
VSTPluginFormat::VSTPluginFormat()
{
}

VSTPluginFormat::~VSTPluginFormat()
{
}

void VSTPluginFormat::findAllTypesForFile (OwnedArray <PluginDescription>& results,
                                           const String& fileOrIdentifier)
{
    if (! fileMightContainThisPluginType (fileOrIdentifier))
        return;

    PluginDescription desc;
    desc.fileOrIdentifier = fileOrIdentifier;
    desc.uid = 0;

    ScopedPointer <VSTPluginInstance> instance (dynamic_cast <VSTPluginInstance*> (createInstanceFromDescription (desc)));

    if (instance == 0)
        return;

    try
    {
#if JUCE_MAC
        if (instance->module->resFileId != 0)
            UseResFile (instance->module->resFileId);
#endif

        instance->fillInPluginDescription (desc);

        VstPlugCategory category = (VstPlugCategory) instance->dispatch (effGetPlugCategory, 0, 0, 0, 0);

        if (category != kPlugCategShell)
        {
            // Normal plugin...
            results.add (new PluginDescription (desc));

            ++insideVSTCallback;
            instance->dispatch (effOpen, 0, 0, 0, 0);
            --insideVSTCallback;
        }
        else
        {
            // It's a shell plugin, so iterate all the subtypes...
            char shellEffectName [64];

            for (;;)
            {
                zerostruct (shellEffectName);
                const int uid = instance->dispatch (effShellGetNextPlugin, 0, 0, shellEffectName, 0);

                if (uid == 0)
                {
                    break;
                }
                else
                {
                    desc.uid = uid;
                    desc.name = shellEffectName;

                    bool alreadyThere = false;

                    for (int i = results.size(); --i >= 0;)
                    {
                        PluginDescription* const d = results.getUnchecked(i);

                        if (d->isDuplicateOf (desc))
                        {
                            alreadyThere = true;
                            break;
                        }
                    }

                    if (! alreadyThere)
                        results.add (new PluginDescription (desc));
                }
            }
        }
    }
    catch (...)
    {
        // crashed while loading...
    }
}

AudioPluginInstance* VSTPluginFormat::createInstanceFromDescription (const PluginDescription& desc)
{
    ScopedPointer <VSTPluginInstance> result;

    if (fileMightContainThisPluginType (desc.fileOrIdentifier))
    {
        File file (desc.fileOrIdentifier);

        const File previousWorkingDirectory (File::getCurrentWorkingDirectory());
        file.getParentDirectory().setAsCurrentWorkingDirectory();

        const ReferenceCountedObjectPtr <ModuleHandle> module (ModuleHandle::findOrCreateModule (file));

        if (module != 0)
        {
            shellUIDToCreate = desc.uid;

            result = new VSTPluginInstance (module);

            if (result->effect != 0)
            {
                result->effect->resvd2 = (VstIntPtr) (pointer_sized_int) (VSTPluginInstance*) result;
                result->initialise();
            }
            else
            {
                result = 0;
            }
        }

        previousWorkingDirectory.setAsCurrentWorkingDirectory();
    }

    return result.release();
}

bool VSTPluginFormat::fileMightContainThisPluginType (const String& fileOrIdentifier)
{
    const File f (fileOrIdentifier);

#if JUCE_MAC
    if (f.isDirectory() && f.hasFileExtension (T(".vst")))
        return true;

#if JUCE_PPC
    FSRef fileRef;
    if (PlatformUtilities::makeFSRefFromPath (&fileRef, f.getFullPathName()))
    {
        const short resFileId = FSOpenResFile (&fileRef, fsRdPerm);

        if (resFileId != -1)
        {
            const int numEffects = Count1Resources ('aEff');
            CloseResFile (resFileId);

            if (numEffects > 0)
                return true;
        }
    }
#endif

    return false;
#elif JUCE_WIN32
    return f.existsAsFile()
            && f.hasFileExtension (T(".dll"));
#elif JUCE_LINUX
    return f.existsAsFile()
            && f.hasFileExtension (T(".so"));
#endif
}

const String VSTPluginFormat::getNameOfPluginFromIdentifier (const String& fileOrIdentifier)
{
    return fileOrIdentifier;
}

bool VSTPluginFormat::doesPluginStillExist (const PluginDescription& desc)
{
    return File (desc.fileOrIdentifier).exists();
}

const StringArray VSTPluginFormat::searchPathsForPlugins (const FileSearchPath& directoriesToSearch, const bool recursive)
{
    StringArray results;

    for (int j = 0; j < directoriesToSearch.getNumPaths(); ++j)
        recursiveFileSearch (results, directoriesToSearch [j], recursive);

    return results;
}

void VSTPluginFormat::recursiveFileSearch (StringArray& results, const File& dir, const bool recursive)
{
    // avoid allowing the dir iterator to be recursive, because we want to avoid letting it delve inside
    // .component or .vst directories.
    DirectoryIterator iter (dir, false, "*", File::findFilesAndDirectories);

    while (iter.next())
    {
        const File f (iter.getFile());
        bool isPlugin = false;

        if (fileMightContainThisPluginType (f.getFullPathName()))
        {
            isPlugin = true;
            results.add (f.getFullPathName());
        }

        if (recursive && (! isPlugin) && f.isDirectory())
            recursiveFileSearch (results, f, true);
    }
}

const FileSearchPath VSTPluginFormat::getDefaultLocationsToSearch()
{
#if JUCE_MAC
    return FileSearchPath ("~/Library/Audio/Plug-Ins/VST;/Library/Audio/Plug-Ins/VST");
#elif JUCE_WIN32
    const String programFiles (File::getSpecialLocation (File::globalApplicationsDirectory).getFullPathName());

    return FileSearchPath (programFiles + "\\Steinberg\\VstPlugins");
#elif JUCE_LINUX
    return FileSearchPath ("/usr/lib/vst");
#endif
}


END_JUCE_NAMESPACE

#endif

#undef log

#endif
