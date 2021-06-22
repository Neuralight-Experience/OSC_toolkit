/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace std;

//==============================================================================
Festivalle21AudioProcessor::Festivalle21AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
#ifdef MEASURE_TIME
    this->myfile.open("timing_measure.txt");
#endif

    this->sampleRate = 0.0;
    this->samplesPerBlock = 0.0;
    this->bufferToFillSampleIdx = 0;
    this->bufferToFill.setSize(1, BUFFER_SIZE);
    this->av = std::vector<std::vector<float>>(COLOR_FREQUENCY, std::vector<float>(2, 0));
    this->rms = 0.0f;
    this->currentAVindex = 0;

    this->avgArousal = 0;
    this->avgValence = 0;
    this->R = 0;
    this->G = 0;
    this->B = 0;

    this->oscIpAddress = "127.0.0.1";
    this->oscPort = 5005;
    this->connectToOsc();
}

Festivalle21AudioProcessor::~Festivalle21AudioProcessor()
{
#ifdef MEASURE_TIME
    this->myfile.close();
#endif
}

//==============================================================================
const juce::String Festivalle21AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Festivalle21AudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool Festivalle21AudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool Festivalle21AudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double Festivalle21AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Festivalle21AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int Festivalle21AudioProcessor::getCurrentProgram()
{
    return 0;
}

void Festivalle21AudioProcessor::setCurrentProgram(int index)
{
}

const juce::String Festivalle21AudioProcessor::getProgramName(int index)
{
    return {};
}

void Festivalle21AudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void Festivalle21AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    this->sampleRate = sampleRate;
    this->samplesPerBlock = samplesPerBlock;
    //this->bufferToFill.resize(sampleRate * BUFFER_SIZE); // Initiliase the array to contain 500ms
   // this->bufferToFill.resize(22050); // Initiliase the array to contain 500ms
}

void Festivalle21AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Festivalle21AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void Festivalle21AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();


    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.

    if (this->connected)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); sample++)
        {
            float monoSample = 0.0;

            for (int channel = 0; channel < totalNumInputChannels; ++channel)
            {
                auto* inputChannelData = buffer.getReadPointer(channel);
                monoSample += inputChannelData[sample];
            }

            monoSample /= totalNumInputChannels;
            this->bufferToFill.getWritePointer(0)[this->bufferToFillSampleIdx] = monoSample;

            this->bufferToFillSampleIdx++;
            if (this->bufferToFillSampleIdx == BUFFER_SIZE)
            {
                this->bufferToFillSampleIdx = 0;

#ifdef MEASURE_TIME
                using std::chrono::high_resolution_clock;
                using std::chrono::duration_cast;
                using std::chrono::duration;
                using std::chrono::milliseconds;

                auto t1 = high_resolution_clock::now();
#endif

                this->av.at(currentAVindex) = this->predictAV(this->bufferToFill);

#ifdef MEASURE_TIME
                auto t2 = high_resolution_clock::now();

                /* Getting number of milliseconds as an integer. */
                auto ms_int = duration_cast<milliseconds>(t2 - t1);

                /* Getting number of milliseconds as a double. */
                duration<double, std::milli> ms_double = t2 - t1;
                this->myfile << to_string(ms_double.count());
                this->myfile << "\n";
                DBG(ms_double.count());
#endif

                this->rms = this->bufferToFill.getRMSLevel(0, 0, BUFFER_SIZE);
                //this->rms = max(20 * log(this->rms / 20), -100.f);               //convert to dB
                this->rms = max(20 * log(this->rms / 20) / 2, -100.f);               //convert to dB
                DBG(this->rms);

                this->calculateRGB();
                sender.send("/juce/RGB", juce::OSCArgument(this->R), juce::OSCArgument(this->G), juce::OSCArgument(this->B));


                this->currentAVindex++;
                if (this->currentAVindex == COLOR_FREQUENCY) {
                    std::vector<float> msg;
                    //msg = this->getRGBValue(this->av);
                    this->averageAV(this->av);
                    //this->calculateRGB();
                    //msg = this->getRGB();
                    // create and send an OSC message with an address and a float value:
                    //sender.send("/juce/RGB", juce::OSCArgument(this->R), juce::OSCArgument(this->G), juce::OSCArgument(this->B));
                    this->currentAVindex = 0;
                }
            }
        }

    }


}

//==============================================================================
bool Festivalle21AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* Festivalle21AudioProcessor::createEditor()
{
    return new Festivalle21AudioProcessorEditor(*this);
}

//==============================================================================
void Festivalle21AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void Festivalle21AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

std::vector<float> Festivalle21AudioProcessor::getAV()
{
    return this->av.at(0);
}

bool Festivalle21AudioProcessor::setIP(juce::String ipAddress)
{
    this->oscIpAddress = ipAddress;    
    this->connectToOsc();
    return this->connected;
}

bool Festivalle21AudioProcessor::setPort(juce::String port)
{
    this->oscPort = port.getIntValue();
    this->connectToOsc();
    return this->connected;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Festivalle21AudioProcessor();
}

std::vector<float> Festivalle21AudioProcessor::predictAV(juce::AudioBuffer<float> buffer)
{
    std::vector<float> my_vector{ buffer.getReadPointer(0), buffer.getReadPointer(0) + BUFFER_SIZE };
    const fdeep::tensor input(fdeep::tensor_shape(22050, 1), my_vector);

    const fdeep::tensors result = model.predict({ input });

    return result[0].to_vector();
}

void Festivalle21AudioProcessor::calculateRGB()
{
    float H = (atan2(this->avgValence, this->avgArousal) * 180.0 / PI);    // Hue
    if (H < 0) {
        H = 360.0 + H;
    }
    DBG("H: " + to_string(H));
    float S = min(sqrt(pow(this->avgValence, 2) + pow(this->avgArousal, 2)), 1.0);    // Saturation (distance)
    float V = 1.0 + this->rms / 100;  //Intensity

    float C = V * S;
    float X = C * (1.0 - std::abs(std::fmod((H / 60.0), 2.0) - 1.0));
    float m = V - C;

    if (H >= 0 && H < 60) {
        this->R = C;
        this->G = X;
        this->B = 0;
    }
    else if (H >= 60 && H < 120) {
        this->R = X;
        this->G = C;
        this->B = 0;
    }
    else if (H >= 120 && H < 180) {
        this->R = 0;
        this->G = C;
        this->B = X;
    }
    else if (H >= 180 && H < 240) {
        this->R = 0;
        this->G = X;
        this->B = C;
    }
    else if (H >= 240 && H < 300) {
        this->R = X;
        this->G = 0;
        this->B = C;
    }
    else if (H >= 300 && H <= 360) {
        this->R = C;
        this->G = 0;
        this->B = X;
    }

    this->R = (this->R + m);
    this->G = (this->G + m);
    this->B = (this->B + m);

    ryb2RGB();

    DBG("R: " + to_string(this->R));
    DBG("G: " + to_string(this->G));
    DBG("B: " + to_string(this->B));
}

void Festivalle21AudioProcessor::averageAV(std::vector<std::vector<float>> av)
{
    float avg_valence = 0.0f;
    float avg_arousal = 0.0f;

    for (int i = 0; i < COLOR_FREQUENCY; i++) {
        avg_valence += av[i][1];
        avg_arousal += av[i][0];
    }
    avg_valence /= av.size();
    avg_arousal /= av.size();

    this->avgValence = min(1.f, avg_valence * SCALING_FACTOR);
    this->avgArousal = min(1.f, avg_arousal * SCALING_FACTOR);
}

void Festivalle21AudioProcessor::connectToOsc()
{
    // specify here where to send OSC messages to: host URL and UDP port number
    sender.connect(this->oscIpAddress, this->oscPort);   // [4]

    this->oscPort >= 65536 ? this->connected = false : this->connected = true;
}

void Festivalle21AudioProcessor::ryb2RGB()
{
    float x0 = this->cubicInterp(this->B, RYB_COLORS[0][0], RYB_COLORS[4][0]);
    float x1 = this->cubicInterp(this->B, RYB_COLORS[1][0], RYB_COLORS[5][0]);
    float x2 = this->cubicInterp(this->B, RYB_COLORS[2][0], RYB_COLORS[6][0]);
    float x3 = this->cubicInterp(this->B, RYB_COLORS[3][0], RYB_COLORS[7][0]);
    float y0 = this->cubicInterp(this->G, x0, x1);
    float y1 = this->cubicInterp(this->G, x2, x3);

    this->R = this->cubicInterp(this->R, y0, y1);

     x0 = this->cubicInterp(this->B, RYB_COLORS[0][1], RYB_COLORS[4][1]);
     x1 = this->cubicInterp(this->B, RYB_COLORS[1][1], RYB_COLORS[5][1]);
     x2 = this->cubicInterp(this->B, RYB_COLORS[2][1], RYB_COLORS[6][1]);
     x3 = this->cubicInterp(this->B, RYB_COLORS[3][1], RYB_COLORS[7][1]);
     y0 = this->cubicInterp(this->G, x0, x1);
     y1 = this->cubicInterp(this->G, x2, x3);

    this->G = this->cubicInterp(this->R, y0, y1);


    x0 = this->cubicInterp(this->B, RYB_COLORS[0][2], RYB_COLORS[4][2]);
    x1 = this->cubicInterp(this->B, RYB_COLORS[1][2], RYB_COLORS[5][2]);
    x2 = this->cubicInterp(this->B, RYB_COLORS[2][2], RYB_COLORS[6][2]);
    x3 = this->cubicInterp(this->B, RYB_COLORS[3][2], RYB_COLORS[7][2]);
    y0 = this->cubicInterp(this->G, x0, x1);
    y1 = this->cubicInterp(this->G, x2, x3);

    this->B = this->cubicInterp(this->R, y0, y1);

}

float Festivalle21AudioProcessor::cubicInterp(float t, float A, float B)
{
    float w = t * t * (3.0f - 2.0f * t);

    return A + w * (B - A);
}
