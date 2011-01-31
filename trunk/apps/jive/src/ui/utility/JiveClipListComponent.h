
#ifndef JIVECLIPLISTCOMPONENT_H
#define JIVECLIPLISTCOMPONENT_H

#include "Config.h"

class ClipListComponent;

class  ClipListListener
{
public:
    virtual ~ClipListListener() {};

    virtual void clipListChanged(ClipListComponent* ctrlThatHasChanged) = 0;
    virtual void currentClipChanged(ClipListComponent* ctrlThatHasChanged) = 0;
    virtual void clipFilesDropped(ClipListComponent* ctrlThatHasChanged, const StringArray& files) = 0;
};


// A component for holding a list of files, in particular a list of audio or midi clips.
// Supports adding and removing from the list, rearranging items.
// A 'current item' is supported - e.g. the currently playing clip.

// May support midi bindings in future. (Rather than having to have an intermediary ParamSlider.)

//==============================================================================
class ClipListComponent
: 
   public Component,
   public FileDragAndDropTarget,
   public ComboBoxListener, 
   public AudioParameterListener
{
public:
   ClipListComponent(String componentName, String fileBrowserWildcard);
   ~ClipListComponent();

   int getMaxClips() const;
   void setMaxClips(int maxClips);

   int getNumClips() const;
   
   void addClipFile(String filename);
   void setClipFile(int clipIndex, String filename);
   
   String getCurrentClipFile() const;

   int getCurrentClipIndex();
   void setCurrentClipIndex(int index, bool notifyObservers = false);
   
   const StringArray getClipList() const;
   void setClipList(const StringArray& filenames);

   void addListener(ClipListListener* const listener) throw();
   void removeListener(ClipListListener* const listener) throw();

   // Component
   void resized();
   void paintOverChildren(Graphics& g);
//   void mouseDown(const MouseEvent& e);
   
   // FileDragAndDropTarget
   bool isInterestedInFileDrag (const StringArray& files);
   void filesDropped (const StringArray& files, int, int);
   void fileDragEnter (const StringArray& files, int, int);
   void fileDragExit (const StringArray& files);
   
   // ComboBoxListener
   virtual void comboBoxChanged(ComboBox* comboBoxThatHasChanged);
   
   // AudioParameterListener
   void parameterChanged (AudioParameter* newParameter, const int index);

   // broadcasting our changes
   void sendClipListChangedMessage();
   void sendCurrentClipChangedMessage();
   void sendFilesDroppedMessage(const StringArray& files);
   
   
//==============================================================================
juce_UseDebuggingNewOperator

private:
   
int maxClips; // TODO: maxClips is actually a bit of a load of crap here (plugin is responsible)..
int currentClip;
StringArray clipFiles;
String fileBrowserWildcard;
ComboBox* clipsCombo;
bool isFileDragOver;

//AudioProcessor* ownerPlugin;
//AudioParameter* managedParam; 
//const int paramIndex;

SortedSet<void*> listeners;

ClipListComponent (const ClipListComponent&);
const ClipListComponent& operator= (const ClipListComponent&);
};


#endif   // JOST_PARAMSLIDER_H
