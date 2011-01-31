/*
 ==============================================================================

 This file is part of the JUCETICE project - Copyright 2007 by Lucio Asnaghi.

 JUCETICE is based around the JUCE library - "Jules' Utility Class Extensions"
 Copyright 2007 by Julian Storer.

 ------------------------------------------------------------------------------

 JUCE and JUCETICE can be redistributed and/or modified under the terms of
 the GNU Lesser General Public License, as published by the Free Software
 Foundation; either version 2 of the License, or (at your option) any later
 version.

 JUCE and JUCETICE are distributed in the hope that they will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with JUCE and JUCETICE; if not, visit www.gnu.org/licenses or write to
 Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 Boston, MA 02111-1307 USA

 ==============================================================================
*/

#ifndef __JUCETICE_JOSTSUPERMIDISEQUENCEPLUGIN_HEADER__
#define __JUCETICE_JOSTSUPERMIDISEQUENCEPLUGIN_HEADER__

#include "MidiSequencePluginBase.h"

#define MIDISEQ_PARAMID_SEQENABLED MIDISEQ_PARAMID_CURRENTCLIP + 1

//==============================================================================
// properties - 
#define PROP_ENABLEDCCVAL                     T("enabledCCValue")
#define PROP_PARTPATTERNINDEX                      T("patternNum")

//==============================================================================
/**
    A class extending MidiSequencePluginBase with support for controller automation.
	
    @see MidiSequencePluginBase
*/
class MidiSequencePlugin : 
	public MidiSequencePluginBase,
	public AutomationGridListener
{
public:
    //==============================================================================
    /** Construct a midi sequence plugin */
    MidiSequencePlugin ();

    /** Destructor */
    ~MidiSequencePlugin ();

    //==============================================================================
	AudioProcessorEditor* createEditor(); 
	
	void processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages);
		
    //==============================================================================
	void getControllerIndexed (const int index, int& controllerNum, double& value, double& beat);
	
	int getNumControllerEvents () const;
	
    //==============================================================================
	bool eventAdded (const int controller, const double automationValue,
                            const float beatNumber);

    bool eventRemoved (const int controller, const double automationValue,
                            const float beatNumber);

    bool eventMoved (const int controllerNum,const double oldValue,
                            const float oldBeat,
                            const double automationValue,
                            const float beatNumber);

    bool allEventsRemoved ();

    //==============================================================================
    /** Serialize internal properties to an Xml element */
    void savePropertiesToXml (XmlElement* element);

    /** Deserialize internal properties from an Xml element */
    void loadPropertiesFromXml (XmlElement* element);

    //==============================================================================
    void setParameterReal (int paramNumber, float value);
    float getParameterReal (int paramNumber);
    const String getParameterTextReal (int paramNumber, float value);
   
   // set this sequence to be a pattern in a part (i.e. verse bass, chorus bass etc)
   // 
   void setPatternNumberInPart(int indexOfPattern);
   int getPatternNumberInPart() {return partNumber;};
   
protected:
   virtual bool isEnabled();
   
private:
   float enableCCMax;
   float enableCCMin;
   float enableCCValue;
   int partNumber;
   bool isEnabledRightNow;
};


#endif
