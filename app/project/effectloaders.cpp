#include "effectloaders.h"

#include "project/effect.h"
#include "project/transition.h"
#include "io/path.h"
#include "panels/panels.h"
#include "panels/effectcontrols.h"

#include <QDir>
#include <QXmlStreamReader>

#include <QDebug>

#include <frei0r/frei0r.h>
typedef void (*f0rGetPluginInfo)(f0r_plugin_info_t* info);

void load_internal_effects() {
	if (!shaders_are_enabled) qWarning() << "Shaders are disabled, some effects may be nonfunctional";

	EffectMeta em;

	// internal effects
	em.type = EFFECT_TYPE_EFFECT;
	em.subtype = EFFECT_TYPE_AUDIO;

	em.name = "Volume";
	em.internal = EFFECT_INTERNAL_VOLUME;
	effects.append(em);

	em.name = "Pan";
	em.internal = EFFECT_INTERNAL_PAN;
	effects.append(em);

#ifdef _WIN32
	em.name = "VST Plugin 2.x";
	em.internal = EFFECT_INTERNAL_VST;
	effects.append(em);
#endif

	em.name = "Tone";
	em.internal = EFFECT_INTERNAL_TONE;
	effects.append(em);

	em.name = "Noise";
	em.internal = EFFECT_INTERNAL_NOISE;
	effects.append(em);

	em.name = "Fill Left/Right";
	em.internal = EFFECT_INTERNAL_FILLLEFTRIGHT;
	effects.append(em);

	em.subtype = EFFECT_TYPE_VIDEO;

	em.name = "Transform";
	em.category = "Distort";
	em.internal = EFFECT_INTERNAL_TRANSFORM;
	effects.append(em);

	em.name = "Corner Pin";
	em.internal = EFFECT_INTERNAL_CORNERPIN;
	effects.append(em);

	/*em.name = "Mask";
	em.internal = EFFECT_INTERNAL_MASK;
	effects.append(em);*/

	em.name = "Shake";
	em.internal = EFFECT_INTERNAL_SHAKE;
	effects.append(em);

	em.name = "Text";
	em.category = "Render";
	em.internal = EFFECT_INTERNAL_TEXT;
	effects.append(em);

	em.name = "Timecode";
	em.internal = EFFECT_INTERNAL_TIMECODE;
	effects.append(em);

	em.name = "Solid";
	em.internal = EFFECT_INTERNAL_SOLID;
	effects.append(em);

	// internal transitions
	em.type = EFFECT_TYPE_TRANSITION;
	em.category = "";

	em.name = "Cross Dissolve";
	em.internal = TRANSITION_INTERNAL_CROSSDISSOLVE;
	effects.append(em);

	em.subtype = EFFECT_TYPE_AUDIO;

	em.name = "Linear Fade";
	em.internal = TRANSITION_INTERNAL_LINEARFADE;
	effects.append(em);

	em.name = "Exponential Fade";
	em.internal = TRANSITION_INTERNAL_EXPONENTIALFADE;
	effects.append(em);

	em.name = "Logarithmic Fade";
	em.internal = TRANSITION_INTERNAL_LOGARITHMICFADE;
	effects.append(em);
}

void load_shader_effects() {
	QList<QString> effects_paths = get_effects_paths();

	for (int h=0;h<effects_paths.size();h++) {
		const QString& effects_path = effects_paths.at(h);
		QDir effects_dir(effects_path);
		if (effects_dir.exists()) {
			QList<QString> entries = effects_dir.entryList(QStringList("*.xml"), QDir::Files);
			for (int i=0;i<entries.size();i++) {
				QFile file(effects_path + "/" + entries.at(i));
				if (!file.open(QIODevice::ReadOnly)) {
					qCritical() << "Could not open" << entries.at(i);
					return;
				}

				QXmlStreamReader reader(&file);
				while (!reader.atEnd()) {
					if (reader.name() == "effect") {
						QString effect_name = "";
						QString effect_cat = "";
						const QXmlStreamAttributes attr = reader.attributes();
						for (int j=0;j<attr.size();j++) {
							if (attr.at(j).name() == "name") {
								effect_name = attr.at(j).value().toString();
							} else if (attr.at(j).name() == "category") {
								effect_cat = attr.at(j).value().toString();
							}
						}
						if (!effect_name.isEmpty()) {
							EffectMeta em;
							em.type = EFFECT_TYPE_EFFECT;
							em.subtype = EFFECT_TYPE_VIDEO;
							em.name = effect_name;
							em.category = effect_cat;
							em.filename = file.fileName();
							em.path = effects_path;
							em.internal = -1;
							effects.append(em);
						} else {
							qCritical() << "Invalid effect found in" << entries.at(i);
						}
						break;
					}
					reader.readNext();
				}

				file.close();
			}
		}
	}
}

void init_effects() {
	EffectInit* init_thread = new EffectInit();
	QObject::connect(init_thread, SIGNAL(finished()), init_thread, SLOT(deleteLater()));
	init_thread->start();
}

void load_frei0r_effects() {
	QList<QString> effect_dirs = get_effects_paths();
	int lim = effect_dirs.size();

	// add extra search paths for frei0r effects
	for (int i=0;i<lim;i++) {
		effect_dirs.append(QDir(effect_dirs.at(i)).filePath("frei0r"));
	}

	// add defined paths for frei0r plugins on unix
#if defined(__APPLE__) || defined(__linux__)

#endif

	// search for paths
	EffectMeta em;
	em.category = "Frei0r";
	em.type = EFFECT_TYPE_EFFECT;
	em.subtype = EFFECT_TYPE_VIDEO;
	em.internal = EFFECT_INTERNAL_FREI0R;
	for (int i=0;i<effect_dirs.size();i++) {
		QDir search_dir(effect_dirs.at(i));
		if (search_dir.exists()) {
			QList<QString> entry_list = search_dir.entryList(QStringList("*.dll"));
			for (int j=0;j<entry_list.size();j++) {

				HMODULE effect = LoadLibrary(reinterpret_cast<const wchar_t*>(search_dir.filePath(entry_list.at(j)).utf16()));
				if (effect != nullptr) {
					f0rGetPluginInfo get_info_func = reinterpret_cast<f0rGetPluginInfo>(GetProcAddress(effect, "f0r_get_plugin_info"));
					if (get_info_func != nullptr) {
						f0r_plugin_info_t info;
						get_info_func(&info);

						if (info.plugin_type == F0R_PLUGIN_TYPE_FILTER
								&& info.color_model == F0R_COLOR_MODEL_RGBA8888) {
							em.name = info.name;
							em.path = effect_dirs.at(i);
							em.filename = entry_list.at(j);

							effects.append(em);
						}

//						qDebug() << "Found:" << info.name << "by" << info.author;
					}
					FreeLibrary(effect);
				}


//				qDebug() << search_dir.filePath(entry_list.at(j));
			}
		}
	}
}

EffectInit::EffectInit() {
	panel_effect_controls->effects_loaded.lock();
}

void EffectInit::run() {
	qInfo() << "Initializing effects...";
	load_internal_effects();
	load_shader_effects();
	load_frei0r_effects();
	panel_effect_controls->effects_loaded.unlock();
	qInfo() << "Finished initializing effects";
}
