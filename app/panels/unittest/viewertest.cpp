#include "viewertest.h"

#include <QtTest>

#include "panels/viewer.h"

ViewerTest::ViewerTest(QObject *parent) : QObject(parent)
{

}



void ViewerTest::testCaseCreateFootageSequenceAudioVideoClip()
{
  // create a seemingly valid media instance. 1 audio track. 1 video track
  auto mda = std::make_shared<Media>();
  auto ftg = std::make_shared<Footage>();
  mda->setFootage(ftg);

  auto strm = std::make_shared<project::FootageStream>();
  ftg->video_tracks.append(strm);
  ftg->audio_tracks.append(strm);

  Viewer vwr;
  auto sqn = vwr.createFootageSequence(mda);
  QVERIFY(sqn->clips_.size() == 2);
}
