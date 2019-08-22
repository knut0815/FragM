#include <QStringList>
#include <QRegExp>
#include <QMap>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QVector>


#include <glm/glm.hpp>

#include "../../SyntopiaCore/Exceptions/Exception.h"
#include "../../SyntopiaCore/Logging/Logging.h"
#include "Preprocessor.h"

using namespace SyntopiaCore::Exceptions;
using namespace SyntopiaCore::Logging;


namespace Fragmentarium
{
namespace Parser
{

FragmentSource::FragmentSource() :
    hasPixelSizeUniform(false),
    bufferShaderSource(nullptr),
    clearOnChange(true),
    iterationsBetweenRedraws(0),
    subframeMax(-1),
    depthToAlpha(false),
    autoFocus(false)
{
}

// Helpers:
namespace
{
float parseFloat(QString s)
{
    bool success = false;
    float to = s.toFloat(&success);
    if (!success) {
        WARNING("Could not parse float: " + s);
        return 0;
    }
    return to;
}

// double parseDouble ( QString s ) {
//     bool success = false;
//     double to = s.toDouble ( &success );
//     if ( !success ) {
//         WARNING ( "Could not parse double: " + s );
//         return 0;
//     }
//     return to;
// }

void setLockType(GuiParameter *p, QString lockTypeString)
{
    lockTypeString = lockTypeString.toLower().trimmed();
    LockType l = NotLocked;
    if (lockTypeString == "locked") {
        l = Locked;
    } else if (lockTypeString == "notlocked" || lockTypeString.isEmpty()) {
        l = NotLocked;
    } else if (lockTypeString == "notlockable") {
        l = NotLockable;
    }  else if (lockTypeString == "alwayslocked") {
        l = AlwaysLocked;
    } else {
        WARNING("Not able to parse lock type: " + lockTypeString + " Default notlocked!");
    }
    p->setLockType(l);
}

void setSliderType(GuiParameter *p, QString sliderTypeString)
{
    sliderTypeString = sliderTypeString.toLower().trimmed();
    SliderType l = Linear;
    if (sliderTypeString == "logarithmic") {
        l = Logarithmic;
    } else if (sliderTypeString == "linear" || sliderTypeString.isEmpty()) {
        l = Linear;
    } else {
        WARNING("Not able to parse slider type: " + sliderTypeString + " Default linear!");
    }
    p->setSliderType(l);
}

glm::dvec4 parseQVector4D(QString s1, QString s2, QString s3, QString s4)
{
    return { parseFloat(s1), parseFloat(s2), parseFloat(s3), parseFloat(s4) };
}

glm::dvec3 parseQVector3D(QString s1, QString s2, QString s3)
{
    return { parseFloat(s1), parseFloat(s2), parseFloat(s3) };
}

glm::dvec2 parseQVector2D(QString s1, QString s2)
{
    return { parseFloat(s1), parseFloat(s2) };
}
}

void Preprocessor::parseSource(FragmentSource *fs, QString input, QString originalFileName, bool dontAdd)
{
    fs->sourceFileNames.append(originalFileName);
    int sf = fs->sourceFileNames.count() - 1;

    QRegExp includeCommand("^#include(.*)\\s\"([^\"]+)\"\\s*$");    // Look for #include "test.frag"
    QRegExp bufferShaderCommand("^#buffershader\\s\"([^\"]+)\"\\s*$");    // Look for #buffershader "test.frag"

    QStringList in = input.split(QRegExp("\r\n|\r|\n"));
    if (!isCreatingAutoSave) in.append("#group default");      // make sure we fall back to the default group after including a file.

    int originalFileIndex = fs->sourceFileNames.indexOf(originalFileName);
    // scan for included files
    bool found = false;
    int lastInc=0;
    // figure out the line number for #include statement in this file
    for (int i = 0; i < in.count(); i++) {
        if (includeCommand.indexIn(in[i]) != -1) {
            QString post = includeCommand.cap(1);
            if (post == "") {
                found = true;
                lastInc=i;
            } else {
                throw Exception("'#include' expected");
            }
        }
        if(in[i].startsWith("#vertex")) {
            in.insert( i+1, QString("#line %1 %2").arg(i+1).arg(originalFileIndex) );
        }
    }

    // we don't use fs->source or fs->lines etc because the file hasn't been parsed yet and we add some lines
    if(!found) { // this file has no #include statements
        in.insert( 1, QString("#line %1 %2").arg(1).arg(originalFileIndex) );
    }
    else // insert #line directive after last #include statement
    {
        in.insert( lastInc+1, QString("#line %1 %2").arg(lastInc+1).arg(originalFileIndex) );
    }

    QList<int> lines;
    for (int i = 0; i < in.count(); i++) lines.append(i);
    lines.append(-1);

    QList<int> source;
    for (int i = 0; i < in.count(); i++) source.append(sf);
    source.append(-1);

    for (int i = 0; i < in.count(); i++) {
        if (includeCommand.indexIn(in[i]) != -1) {
            QString fileName =  includeCommand.cap(2);
            QString post = includeCommand.cap(1);
            if (post == "") {
            } else {
                throw Exception("'#include' expected");
            }
            QString fName;
            try {
                fName = fileManager->resolveName(fileName, originalFileName);
            } catch (Exception &e) {
                CRITICAL(e.getMessage());
                continue;
            }
            QFile f(fName);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                throw Exception("Unable to open: " +  fName);

            INFO(QCoreApplication::translate("Preprocessor", "Including file: ") + fName);
            QString a = f.readAll();
            parseSource(fs, a, fName, isCreatingAutoSave);
            dependencies.append(fName);
            if (!dontAdd && isCreatingAutoSave) {
                fs->lines.append(lines[i]);
                fs->sourceFile.append(source[i]);
                fs->source.append(in[i]);
            }
        } else if (bufferShaderCommand.indexIn(in[i]) != -1) {
            QString fileName =  bufferShaderCommand.cap(1);
            QString fName;
            try {
                fName = fileManager->resolveName(fileName, originalFileName);
            } catch (Exception &e) {
                CRITICAL(e.getMessage());
                continue;
            }
            QFile f(fName);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                throw Exception("Unable to open: " +  fName);

            INFO(QCoreApplication::translate("Preprocessor", "Including buffershader: ") + fName);
            QString a = f.readAll();
            FragmentSource bs = parse(a, fName, false);
            fs->bufferShaderSource = new FragmentSource(bs);
            dependencies.append(fName);
            if (!dontAdd && isCreatingAutoSave) {
                fs->lines.append(lines[i]);
                fs->sourceFile.append(source[i]);
                fs->source.append(in[i]);
            }
        } else {
            if (!dontAdd) {
                fs->lines.append(lines[i]);
                fs->sourceFile.append(source[i]);
                fs->source.append(in[i]);
            }
        }
    }
}

// We leak here, but fs's are copied!
FragmentSource::~FragmentSource() = default;
// {
/*foreach (QFile* f, sourceFiles) delete(f);*/
//delete(screenShaderSource);
// }

FragmentSource Preprocessor::createAutosaveFragment(QString input, QString file)
{
    FragmentSource fs;
    dependencies.clear();
    isCreatingAutoSave = true;

    // Run the preprocessor...
    parseSource(&fs, input, file, false);

    // And remove any presets...
    for (int i = 0; i < fs.source.count(); i++) {
        QString s = fs.source[i];
        if (s.trimmed().startsWith("#preset", Qt::CaseInsensitive) && !s.right(12).startsWith("KeyFrame", Qt::CaseInsensitive)) {
            s = s.remove("#preset", Qt::CaseInsensitive).trimmed();
            QString name = s;
            fs.source.removeAt(i);
            fs.lines.removeAt(i);
            int j = i;
            bool foundEnd = false;
            while (j < fs.source.count()) {
                if (fs.source[j].trimmed().startsWith("#endpreset", Qt::CaseInsensitive)) {
                    fs.source.removeAt(j);
                    fs.lines.removeAt(j);
                    foundEnd = true;
                    break;
                } else {
                    fs.source.removeAt(j);
                    fs.lines.removeAt(j);
                }
            }
            if (!foundEnd) WARNING("Did not find #endpreset");
            if (i == fs.source.count()) break;
            s = fs.source[i];
        }
    }

    return fs;
}

void Preprocessor::parsePreset(FragmentSource *fs, QString s, int i)
{
    s = s.remove("#preset", Qt::CaseInsensitive).trimmed();
    QString name = s;
    QStringList preset;
    fs->source.removeAt(i);
    fs->lines.removeAt(i);
    int j = i;
    bool foundEnd = false;
    while (j < fs->source.count()) {
        if (fs->source[j].trimmed().startsWith("#endpreset", Qt::CaseInsensitive)) {
            fs->source.removeAt(j);
            fs->lines.removeAt(j);
            foundEnd = true;
            break;
        } else {
            preset.append(fs->source[j]);
            fs->source.removeAt(j);
            fs->lines.removeAt(j);
        }
    }
    if (!foundEnd) WARNING("Did not find #endpreset");
    fs->presets[name] = preset.join("\n");
}

void Preprocessor::parseReplacement(FragmentSource *fs, QString s, int i)
{
    for (QMap<QString, QString>::const_iterator it = replaceMap.constBegin(); it != replaceMap.constEnd(); ++it) {
        if (s.contains(it.key())) {
            fs->source[i] = s.replace(it.key(), replaceMap[it.key()]);
            //INFO("Replacing: " + s + " --> " + fs.source[i]);
            s = fs->source[i];
        }
    }
}

void Preprocessor::parseSpecial(FragmentSource *fs, QString s, int i, bool moveMain)
{
    if (s.trimmed().startsWith("#camera", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
        QString c = s.remove("#camera", Qt::CaseInsensitive);
        fs->camera = c.trimmed();
    } else if (s.trimmed().startsWith("#TexParameter", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
        QString c = s.remove("#TexParameter", Qt::CaseInsensitive).trimmed();
        QStringList l = c.split(" ", QString::SkipEmptyParts);
        if (l.count() != 3) {
            WARNING("#TexParameter expects three arguments! Found: " + l.join(","));
        } else {
            fs->textureParams[l[0]][l[1]] = l[2];
        }
    } else if (s.trimmed().split(" ").at(0) == ("#buffer")) {
        fs->source[i] = "// " + s;
        QString c = s.remove("#buffer", Qt::CaseInsensitive);
        fs->buffer = c.trimmed();
    } else if (s.trimmed().startsWith("#donotrun", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
    } else if (s.trimmed().startsWith("#group", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
        QString c = s.remove("#group", Qt::CaseInsensitive);
        currentGroup = c.trimmed();
    } else if (s.trimmed().startsWith("#vertex", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
        inVertex = true;
    } else if (s.contains("#endvertex", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
        inVertex = false;
    } else if (s.trimmed().startsWith("#info", Qt::CaseInsensitive)) {
        fs->source[i] = "// " + s;
        QString c = s.remove("#info", Qt::CaseInsensitive).trimmed();
        SCRIPTINFO(c);
    } else if (!inVertex && moveMain && main.indexIn(s) != -1) {
        //INFO("Found main: " + s );
        fs->source[i] = s.replace(" main", " fragmentariumMain");
    }  else if (replace.indexIn(s) != -1) {
        QString from = replace.cap(1);
        QString to = replace.cap(2);
        fs->source[i] = "//" + fs->source[i];
        //INFO("Replace rule: '" + from + "' --> '" + to + "'.");
        replaceMap[from] = to;

    }

}

void Preprocessor::parseSampler2D(FragmentSource *fs, int i, QString file)
{
    QString name = sampler2D.cap(1);
    fs->source[i] = "uniform sampler2D " + name + ";";
    QString fileName;
    try {
        fileName = fileManager->resolveName(sampler2D.cap(2), file);
    } catch (Exception &e) {
        CRITICAL(e.getMessage());
    }
    if (QFileInfo(fileName).isFile()) {
        INFO("Added texture: " + name + " -> " + fileName);
    }
    fs->textures[name] = fileName;
    SamplerParameter *sp = new SamplerParameter(currentGroup, name, lastComment, fileName);
    setLockType(sp, "alwayslocked");
    fs->params.append(sp);
}

void Preprocessor::parseSamplerCube(FragmentSource *fs, int i, QString file)
{
    QString name = samplerCube.cap(1);
    fs->source[i] = "uniform samplerCube " + name + ";";
    QString fileName;
    try {
        fileName = fileManager->resolveName(samplerCube.cap(2), file);
    } catch (Exception &e) {
        CRITICAL(e.getMessage());
    }
    if (QFileInfo(fileName).isFile()) {
        INFO("Added Cube texture: " + name + " -> " + fileName);
    }
    fs->textures[name] = fileName;
    SamplerParameter *sp = new SamplerParameter(currentGroup, name, lastComment, fileName);
    setLockType(sp, "alwayslocked");
    fs->params.append(sp);
}

void Preprocessor::parseFloatColorChooser(FragmentSource *fs, int i)
{
    QString type = floatColorChooser.cap(1);
    QString name = floatColorChooser.cap(2);
    fs->source[i] = "uniform " + type + "  " + name + ";"; // uniform location
    QString fromS = floatColorChooser.cap(3);
    QString defS = floatColorChooser.cap(4);
    QString toS = floatColorChooser.cap(5);
    glm::dvec3 defaults = parseQVector3D(floatColorChooser.cap(6), floatColorChooser.cap(7), floatColorChooser.cap(8));

    bool succes = false;
    double from = fromS.toDouble(&succes);
    bool succes2 = false;
    double def = defS.toDouble(&succes2);
    bool succes3 = false;
    double to = toS.toDouble(&succes3);
    if (!succes || !succes2 || !succes3) {
        WARNING("Could not parse color value for uniform: " + name);
        return;//continue;
    }

    FloatColorParameter *fp = new FloatColorParameter(currentGroup, name, lastComment, def, from, to, defaults);
    setLockType(fp, floatColorChooser.cap(9));
    if (type.startsWith("d")) fp->setIsDouble(true);
    fs->params.append(fp);
}

void Preprocessor::parseFloat1Slider(FragmentSource *fs, int i)
{
    QString type = float1Slider.cap(1);
    QString name = float1Slider.cap(2);
    fs->source[i] = "uniform " + type + "  " + name + ";"; // uniform location
    QString fromS = float1Slider.cap(3);
    QString defS = float1Slider.cap(4);
    QString toS = float1Slider.cap(5);

    bool succes = false;
    double from = fromS.toDouble(&succes);
    bool succes2 = false;
    double def = defS.toDouble(&succes2);
    bool succes3 = false;
    double to = toS.toDouble(&succes3);
    if (!succes || !succes2 || !succes3) {
        WARNING("Could not parse float value for uniform: " + name);
        return;//continue;
    }

    FloatParameter *fp = new FloatParameter(currentGroup, name, lastComment, from, to, def);
    setSliderType(fp, float1Slider.cap(6));
    setLockType(fp, float1Slider.cap(7));
    if (type.startsWith("d")) fp->setIsDouble(true);
    fs->params.append(fp);
}

void Preprocessor::parseFloat2Slider(FragmentSource *fs, int i)
{
    QString type = float2Slider.cap(1);
    QString name = float2Slider.cap(2);
    fs->source[i] = "uniform " + type + " " + name + ";"; // uniform location
    glm::dvec2 from = parseQVector2D(float2Slider.cap(3), float2Slider.cap(4));
    glm::dvec2 defaults = parseQVector2D(float2Slider.cap(5), float2Slider.cap(6));
    glm::dvec2 to = parseQVector2D(float2Slider.cap(7), float2Slider.cap(8));

    Float2Parameter *fp = new Float2Parameter(currentGroup, name, lastComment, from, to, defaults);
    setSliderType(fp, float2Slider.cap(9));
    setLockType(fp, float2Slider.cap(10));
    if (type.startsWith("d")) fp->setIsDouble(true);
    fs->params.append(fp);
}

void Preprocessor::parseFloat3Slider(FragmentSource *fs, int i)
{
    QString type = float3Slider.cap(1);
    QString name = float3Slider.cap(2);
    fs->source[i] = "uniform " + type + " " + name + ";"; // uniform location
    glm::dvec3 from = parseQVector3D(float3Slider.cap(3), float3Slider.cap(4), float3Slider.cap(5));
    glm::dvec3 defaults = parseQVector3D(float3Slider.cap(6), float3Slider.cap(7), float3Slider.cap(8));
    glm::dvec3 to = parseQVector3D(float3Slider.cap(9), float3Slider.cap(10), float3Slider.cap(11));

    Float3Parameter *fp = new Float3Parameter(currentGroup, name, lastComment, from, to, defaults);
    setSliderType(fp, float3Slider.cap(12));
    setLockType(fp, float3Slider.cap(13));
    if (type.startsWith("d")) fp->setIsDouble(true);
    fs->params.append(fp);
}

void Preprocessor::parseFloat4Slider(FragmentSource *fs, int i)
{
    QString type = float4Slider.cap(1);
    QString name = float4Slider.cap(2);
    fs->source[i] = "uniform " + type + " " + name + ";"; // uniform location
    glm::dvec4 from = parseQVector4D(float4Slider.cap(3), float4Slider.cap(4), float4Slider.cap(5), float4Slider.cap(6));
    glm::dvec4 defaults = parseQVector4D(float4Slider.cap(7), float4Slider.cap(8), float4Slider.cap(9), float4Slider.cap(10));
    glm::dvec4 to = parseQVector4D(float4Slider.cap(11), float4Slider.cap(12), float4Slider.cap(13), float4Slider.cap(14));

    Float4Parameter *fp = new Float4Parameter(currentGroup, name, lastComment, from, to, defaults);
    setSliderType(fp, float4Slider.cap(15));
    setLockType(fp, float4Slider.cap(16));
    if (type.startsWith("d")) fp->setIsDouble(true);
    fs->params.append(fp);
}

void Preprocessor::parseColorChooser(FragmentSource *fs, int i)
{
    QString type = colorChooser.cap(1);
    QString name = colorChooser.cap(2);
    fs->source[i] = "uniform " + type + " " + name + ";";
    glm::dvec3 defaults = parseQVector3D(colorChooser.cap(3), colorChooser.cap(4), colorChooser.cap(5));
    ColorParameter *cp = new ColorParameter(currentGroup, name, lastComment, defaults);
    setLockType(cp, colorChooser.cap(6));
    if (type.startsWith("d")) cp->setIsDouble(true);
    fs->params.append(cp);
}

void Preprocessor::parseIntSlider(FragmentSource *fs, int i)
{
    QString name = intSlider.cap(1);
    fs->source[i] = "uniform int " + name + ";"; // uniform location
    QString fromS = intSlider.cap(2);
    QString defS = intSlider.cap(3);
    QString toS = intSlider.cap(4);

    bool succes = false;
    int from = fromS.toInt(&succes);
    bool succes2 = false;
    int def = defS.toInt(&succes2);
    bool succes3 = false;
    int to = toS.toInt(&succes3);
    if (!succes || !succes2 || !succes3) {
        WARNING("Could not parse integer value for uniform: " + name);
        return; // continue;
    }

    IntParameter *ip = new IntParameter(currentGroup, name, lastComment, from, to, def);
    setLockType(ip, intSlider.cap(5));
    fs->params.append(ip);
}

void Preprocessor::parseBoolChooser(FragmentSource *fs, int i)
{

    QString name = boolChooser.cap(1);
    fs->source[i] = "uniform bool " + name + ";";
    QString defS = boolChooser.cap(2).toLower().trimmed();

    bool def = false;
    if (defS == "true") {
        def = true;
    } else if (defS == "false") {
        def = false;
    } else {
        WARNING("Could not parse boolean value for uniform: " + name);
        return; // continue;
    }

    BoolParameter *bp = new BoolParameter(currentGroup, name, lastComment, def);
    setLockType(bp, boolChooser.cap(3));
    fs->params.append(bp);
}

void Preprocessor::parseIterations(FragmentSource *fs)
{
    QString iterationCount = iterations.cap(1);
    bool succes = false;
    int i = iterationCount.toInt(&succes);
    if (!succes) {
        WARNING("Could not parse value for 'iterationsbetweenredraws': " + iterationCount);
        return; // continue;
    }
    fs->iterationsBetweenRedraws = i;

}

void Preprocessor::parseMaxSubFrames(FragmentSource *fs)
{
    QString maxCount = subframeMax.cap(1);
    bool succes = false;
    int i = maxCount.toInt(&succes);
    if (!succes) {
        WARNING("Could not parse value for 'subframemax': " + maxCount);
        return; // continue;
    }
    fs->subframeMax = i;

}

FragmentSource Preprocessor::parse(QString input, QString file, bool moveMain)
{

    INFO(QCoreApplication::translate("Preprocessor", "Parse: ") + file);
    FragmentSource fs;
    lastComment = "";
    currentGroup = "";
    inVertex = false;

    // Step one: resolve includes:
    parseSource(&fs, input, file, false);

    // Step two: resolve magic uniforms:
    if (fs.source.indexOf(pixelSizeCommand) != -1) {
        fs.hasPixelSizeUniform = true;
    } else fs.hasPixelSizeUniform = false;
    if (fs.source.indexOf(depthToAlpha) != -1) {
        fs.depthToAlpha = true;
    } else fs.depthToAlpha = false;
    if (fs.source.indexOf(autoFocus) != -1) {
        fs.autoFocus = true;
    } else fs.autoFocus = false;


    for (int i = 0; i < fs.source.count(); i++) {
        QString s = fs.source[i];

        if (s.trimmed().startsWith("#preset", Qt::CaseInsensitive)) {
            parsePreset(&fs, s, i);
            if (i == fs.source.count()) break;
            s = fs.source[i];
        }

        if (!s.contains("#replace", Qt::CaseInsensitive)) {
            parseReplacement(&fs, s, i);
            s = fs.source[i];
        }

        parseSpecial(&fs, s, i, moveMain);

        if (sampler2D.indexIn(s) != -1) {
            parseSampler2D(&fs, i, file);
        } else if (samplerCube.indexIn(s) != -1) {
            parseSamplerCube(&fs, i, file);
        } else if (floatColorChooser.indexIn(s) != -1) {
            parseFloatColorChooser(&fs, i);
        } else if (float1Slider.indexIn(s) != -1) {
            parseFloat1Slider(&fs, i);
        } else if (float2Slider.indexIn(s) != -1) {
            parseFloat2Slider(&fs, i);
        } else if (float3Slider.indexIn(s) != -1) {
            parseFloat3Slider(&fs, i);
        } else if (float4Slider.indexIn(s) != -1) {
            parseFloat4Slider(&fs, i);
        } else if (colorChooser.indexIn(s) != -1) {
            parseColorChooser(&fs, i);
        } else if (intSlider.indexIn(s) != -1) {
            parseIntSlider(&fs, i);
        } else if (boolChooser.indexIn(s) != -1) {
            parseBoolChooser(&fs, i);
        } else if (doneClear.indexIn(s) != -1) {
            fs.clearOnChange = false;
        } else if (iterations.indexIn(s) != -1) {
            parseIterations(&fs);
        } else if (subframeMax.indexIn(s) != -1) {
            parseMaxSubFrames(&fs);
        }

        if (s.trimmed().startsWith("//")) {
            QString c = s.remove("//");
            lastComment = c.trimmed();
        } else {
            lastComment = "";
        }

        if (inVertex && !fs.source[i].contains("#endvertex")) {
            fs.vertexSource.append(fs.source[i]);
            fs.source[i] = "//" + fs.source[i];
        }
    }

    // To ensure main is called as the last command.
    if (moveMain) {
        fs.source.append("");
        fs.source.append("void main() { fragmentariumMain(); }");
        fs.source.append("");
    }

    if (fs.source.count() > 1 && fs.vertexSource.count() > 1) {
        // To ensure that all parts have the same #version
        if (fs.source.at(0).startsWith("#version")) {
            if (!fs.vertexSource.at(0).startsWith("#version")) {
                fs.vertexSource.insert(0, fs.source.at(0));
            }
            if (!(fs.vertexSource.at(0) == fs.source.at(0))) {
                WARNING("Fragment and Vertex do not have the same #version");
                WARNING(QString("Vertex: %1 Fragment: %2").arg(fs.vertexSource.at(0)).arg(fs.source.at(0)));
            }
        }
    }

    return fs;
}
}
}

