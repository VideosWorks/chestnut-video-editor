#include "temporalsmootheffect.h"
#include <omp.h>

TemporalSmoothEffect::TemporalSmoothEffect(ClipPtr c, const EffectMeta* em) : Effect(c, em)
{
  enable_image = true;
  frame_length_ = add_row(tr("Frame Length"))->add_field(EffectFieldType::DOUBLE, "length");
  frame_length_->set_double_minimum_value(1.0);
  frame_length_->set_double_maximum_value(10.0);
  frame_length_->set_double_default_value(3.0);


  blend_mode_ = add_row(tr("Blend Mode"))->add_field(EffectFieldType::COMBO, "mode");
  blend_mode_->add_combo_item("Average", 0);
  blend_mode_->add_combo_item("Median", 0);
  blend_mode_->add_combo_item("Max", 0);
  blend_mode_->add_combo_item("Min", 0);
  blend_mode_->setDefaultValue(0);
}

TemporalSmoothEffect::~TemporalSmoothEffect()
{
  while (!frames_.empty()) {
    delete[] frames_.front();
    frames_.pop_front();
  }
}

uint8_t average(const int ix, const QVector<uint8_t*>& data)
{
  auto val = 0;
  for (const auto& d: data) {
    val += d[ix];
  }
  // average pixel in all frames
  return static_cast<uint8_t>(val / data.size());
}

uint8_t median(const int ix, const QVector<uint8_t*>& data)
{
  const auto sz = data.size();
  uint8_t vals[10];
  for (auto i = 0; i < sz; ++i) {
    vals[i] = data[i][ix];
  }
  std::sort(vals, vals+sz);
  uint8_t val;
  if (sz % 2 == 0) {
    uint16_t tmp = vals[static_cast<size_t>(sz/2)];
    tmp += vals[static_cast<size_t>((static_cast<double>(sz)/2) + 0.5)];
    val = static_cast<uint8_t>((static_cast<double>(tmp) / 2) + 0.5);
  } else {
    val = vals[static_cast<size_t>(sz/2)];
  }
  return val;
}

uint8_t maxVal(const int ix, const QVector<uint8_t*>& data)
{
  uint8_t val = 0;
  for (const auto d : data) {
    if (d[ix] > val) {
      val = d[ix];
    }
  }

  return val;
}

uint8_t minVal(const int ix, const QVector<uint8_t*>& data)
{
  uint8_t val = 255;
  for (const auto d : data) {
    if (d[ix] < val) {
      val = d[ix];
    }
  }

  return val;
}

void TemporalSmoothEffect::process_image(double timecode, uint8_t* data, int size)
{
  if (frame_length_ == nullptr || blend_mode_ == nullptr) {
    qCritical() << "Ui Elements null";
    return;
  }
  const auto length = static_cast<int>(frame_length_->get_double_value(timecode) + 0.5);

  while (frames_.size() > length - 1) { // -1 as new frame is about to be added
    delete[] frames_.front();
    frames_.pop_front();
  }
  // data needs to be copied as is deleted outside of class
  const auto tmp = new uint8_t[static_cast<uint32_t>(size)];
  memcpy(tmp, data, static_cast<size_t>(size));
  frames_.push_back(tmp);

  if (!is_enabled()) {
    return;
  }

  const auto mode_index = blend_mode_->get_combo_index(timecode);
#pragma omp parallel for
  for (auto i=0; i < size; ++i) {
    uint8_t val;
    switch (mode_index) {
      case 0:
        val = average(i, frames_);
        break;
      case 1:
        val = median(i, frames_);
        break;
      case 2:
        val = maxVal(i, frames_);
        break;
      case 3:
        val = minVal(i, frames_);
        break;
      default:
        val = data[i];
        break;
    }
    data[i] = val;
  }
}

