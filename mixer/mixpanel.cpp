#include "mixpanel.h"
#include "mainwindow.h"

MixPanel::MixPanel(QObject *parent) : QObject(parent)
{
    actPos = 0;
    realPosition = 0;
    duration = 0;
    loopingStart = 0;
    isSingleLoop = false;
    isLoopingSet = false;
    isLoopStartSet = false;
    audioReady = false;
    isPlayed = false;
    plot = false;
    speed = 1.0;
    volume = 0.5;

    channel1 = new QByteArray();
    channel2 = new QByteArray();

    QAudioFormat format;
    format.setChannelCount(2);
    format.setCodec("audio/pcm");
    format.setSampleType(QAudioFormat::SignedInt);
    format.setSampleRate(48000);
    format.setSampleSize(16);

    decoder = new QAudioDecoder;
    decoder->setAudioFormat(format);

    connect(decoder, SIGNAL(bufferReady()), this, SLOT(readBuffer()));
    connect(decoder, SIGNAL(finished()), this, SLOT(finishDecoding()));

    memset(&lowMemEq, 0, sizeof(memEQ));
    memset(&medMemEq, 0, sizeof(memEQ));
    memset(&medMemEq2, 0, sizeof(memEQ));
    memset(&highMemEq, 0, sizeof(memEQ));

    lowEQ(50);
    medEQ(50);
    highEQ(50);


}

void MixPanel::playPause() {
    //if(audioReady)
    //{
        isPlayed = !isPlayed;

    //}
}

void MixPanel::playLoopingSet() {
    //if(audioReady)
    //{
        if(isLoopStartSet && isLoopEndSet && !isLoopingSet){
            isLoopStartSet = false;
            //loopingStart = loopingEnd = 0;
        }
        isLoopingSet = !isLoopingSet;

        qDebug() << isLoopingSet;

    //}
}
void MixPanel::playLoopingStart() {
    //if(audioReady)
    //{
        loopingStart = actPos;
        isLoopStartSet = true;

    //}
}
void MixPanel::playLoopingEnd() {
    //if(audioReady)
    //{
        loopingEnd = actPos;
        actPos = loopingStart;
        realPosition = loopingStart;
        isLoopEndSet = true;
    //}
}
void MixPanel::playLoopingReturn() {
    //if(audioReady)
    //{
        if(isLoopStartSet)
        {
            isLoopingSet = true;
            actPos = loopingStart;
            realPosition = loopingStart;
        }
    //}
}
void MixPanel::playLoop() {
    //if(audioReady)
    //{
        isSingleLoop = !isSingleLoop;
    //}
}

void MixPanel::playStop() {

    isPlayed = false;
    audioReady = true;
    actPos = 0;
    realPosition = 0;
    int minutes = duration/1000000./60.;
    int seconds = (duration - minutes*1000000*60)/1000000.;
    QString time  = "0:00/" + QString::number(minutes) + ":" + QString::number(seconds);
    emit timeChange(time);



}

void MixPanel::process(double *buffer, int nFrames) {

    //if(!audioReady || !isPlayed) {
    if( !isPlayed) {
        memset(buffer, 0, sizeof(double)*nFrames*2);
        return;
    }


    for(int i = 0; i < nFrames; i++) {

        if(actPos >= channel1->size()/sizeof(qint16)) {
            buffer[i*2] = 0;
            buffer[i*2+1] = 0;
            continue;
        }

        qint16 value = *(reinterpret_cast<qint16*>(channel1->data())+actPos)*volume;
        double y = processLow(value);
        y = processMedium(y);
        buffer[i*2] = processHigh(y);

        value = *(reinterpret_cast<qint16*>(channel2->data())+actPos)*volume;

        y = processLow(value);
        y = processMedium(y);
        buffer[i*2+1] = processHigh(y);

        realPosition += speed;
        actPos = (int)realPosition;

    }
    if((loopingEnd > loopingStart) && isLoopingSet){
        if((actPos > loopingEnd)){
            actPos = loopingStart;
            realPosition = loopingStart;
        }
    }
    else if ((loopingEnd < loopingStart) && isLoopingSet){
        if((actPos > loopingEnd) && (actPos < loopingStart)){
            actPos = loopingStart;
            realPosition = loopingStart;
        }
    }

    if(actPos >= channel1->size()/sizeof(qint16)) {
        actPos = 0;
        realPosition = 0;
        if(isSingleLoop || isLoopingSet) isPlayed = true;
        else isPlayed = false;
    }

    int seconds = actPos/48000.;
    int minutes = seconds/60.;
    seconds -= minutes*60;

    QString sSeconds = QString::number(seconds);
    if(sSeconds.size() == 1)
        sSeconds.insert(0, '0');

    QString time = QString::number(minutes) + ":" + sSeconds;

    minutes = duration/1000000./60.;
    seconds = (duration - minutes*1000000*60)/1000000.;

    sSeconds = QString::number(seconds);
    if(sSeconds.size() == 1)
        sSeconds.insert(0, '0');

    time  += "/" + QString::number(minutes) + ":" + QString::number(seconds);

    emit timeChange(time);
}

double MixPanel::processEQ(double sample, memEQ &eq) {

    double y = eq.b0*sample + eq.b1*eq.xmem1 + eq.b2*eq.xmem2 - eq.a1*eq.ymem1 - eq.a2*eq.ymem2;

    eq.xmem2 = eq.xmem1;
    eq.xmem1 = sample;
    eq.ymem2 = eq.ymem1;
    eq.ymem1 = y;

    return y;
}

double MixPanel::processLow(double sample) {
    return processEQ(sample, lowMemEq);
}

double MixPanel::processMedium(double sample) {
    return processEQ(processEQ(sample, medMemEq), medMemEq2);
}

double MixPanel::processHigh(double sample) {
    return processEQ(sample, highMemEq);
}

void MixPanel::shelfFilter(double F0, double g, QString type, memEQ &eq) {

    if(type != "low" && type != "high") {
        qDebug() << "Wrong shelf filter type!";
        return;
    }

    double Fs = 48000;
    double S = 1;

    double A = pow(10, g/40.);
    double w0 = 2*3.14159265359*F0/Fs;
    double alpha = sin(w0)/2. * sqrt( (A + 1/A)*(1/S - 1) + 2 );

    int coef = type=="low"?1:-1;

    eq.b0 =        A*( (A+1) - coef*(A-1)*cos(w0) + 2*sqrt(A)*alpha );
    eq.b1 = 2*coef*A*( (A-1) - coef*(A+1)*cos(w0)                   );
    eq.b2 =        A*( (A+1) - coef*(A-1)*cos(w0) - 2*sqrt(A)*alpha );
    eq.a0 =            (A+1) + coef*(A-1)*cos(w0) + 2*sqrt(A)*alpha;
    eq.a1 =  -2*coef*( (A-1) + coef*(A+1)*cos(w0)                   );
    eq.a2 =            (A+1) + coef*(A-1)*cos(w0) - 2*sqrt(A)*alpha;

    eq.b0 /= eq.a0;
    eq.b1 /= eq.a0;
    eq.b2 /= eq.a0;
    eq.a1 /= eq.a0;
    eq.a2 /= eq.a0;
}

void MixPanel::lowEQ(int value) {
    shelfFilter(500, (value-50)/50.*10, "low", lowMemEq);
    emit writeToFile(1, actPos,value);  //emisja sygnalu do zapisania akcji
}

void MixPanel::medEQ(int value) {
    double g = (value-50)/50.*10;
    shelfFilter(15000, g, "low", medMemEq);
    shelfFilter(500, -g, "low", medMemEq2);
    emit writeToFile(2, actPos,value);
}

void MixPanel::highEQ(int value) {
    shelfFilter(15000, (value-50)/50.*10, "high", highMemEq);
    emit writeToFile(3, actPos,value);
}

void MixPanel::loadAudio(QString filename) {
    isPlayed = false;
    audioReady = true;

    duration = 0;
    actPos = 0;
    realPosition = 0;

    channel1->clear();
    channel2->clear();

    decoder->stop();
    decoder->setSourceFilename(filename);
    decoder->start();
}

void MixPanel::readBuffer() {
    QAudioBuffer buffer = decoder->read();
    duration += buffer.duration();

    const qint16 *data = buffer.constData<qint16>();

    for(int i = 0; i < buffer.sampleCount()/2; i++) {
        channel1->append(reinterpret_cast<const char*>(data+i*2), sizeof(qint16));
        channel2->append(reinterpret_cast<const char*>(data+1+i*2), sizeof(qint16));
    }
    int minutes = duration/1000000./60.;
    int seconds = (duration - minutes*1000000*60)/1000000.;

    QString sSeconds = QString::number(seconds);
    if(sSeconds.size() == 1)
        sSeconds.insert(0, '0');

    QString time  = "0:00/" + QString::number(minutes) + ":" + QString::number(seconds);

    emit timeChange(time);
    if(minutes == 1)

    audioReady = true;

}

void MixPanel::finishDecoding() {
    qDebug() << "ready";
    plot = false;

    emit fileReady();

}

void MixPanel::speedChange(int value){
    speed = value/50.0;
}
void MixPanel::volumeChange(int value){
    volume = value/100.0;
}

MixPanel::~MixPanel() {
    delete decoder;

    delete channel1;
    delete channel2;
}






