#include "footagetest.h"
#include "project/footage.h"

FootageTest::FootageTest(QObject *parent) : QObject(parent)
{

}


void FootageTest::testCaseIsImage()
{
  Footage ftg(nullptr);
  ftg.valid = true;
  // by default, not an image
  QVERIFY(ftg.isImage() == false);
  // the sole stream is a video-type and is infinitely long
  auto stream = std::make_shared<project::FootageStream>();
  stream->infinite_length = true;
  ftg.video_tracks.append(stream);
  QVERIFY(ftg.isImage() == true);
  // video-stream has a defined length -> not an image
  stream->infinite_length = false;
  QVERIFY(ftg.isImage() == false);
  // audio and video streams -> not an image
  stream->infinite_length = true;
  ftg.audio_tracks.append(stream);
  QVERIFY(ftg.isImage() == false);
}


void FootageTest::testCaseUninitSave()
{
  Footage ftg(nullptr);
  QString output;
  QXmlStreamWriter stream(&output);
  QVERIFY(ftg.save(stream) == false);
}
