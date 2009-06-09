/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#include "qt4runconfiguration.h"

#include "makestep.h"
#include "profilereader.h"
#include "qt4nodes.h"
#include "qt4project.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/variablemanager.h>
#include <coreplugin/ifile.h>
#include <projectexplorer/buildstep.h>
#include <projectexplorer/environmenteditmodel.h>
#include <utils/qtcassert.h>

#include <QtGui/QFormLayout>
#include <QtGui/QInputDialog>
#include <QtGui/QLabel>
#include <QtGui/QCheckBox>
#include <QtGui/QToolButton>
#include <QtGui/QGroupBox>
#include <QtGui/QRadioButton>

using namespace Qt4ProjectManager::Internal;
using namespace Qt4ProjectManager;
using ProjectExplorer::ApplicationRunConfiguration;
using ProjectExplorer::PersistentSettingsReader;
using ProjectExplorer::PersistentSettingsWriter;

Qt4RunConfiguration::Qt4RunConfiguration(Qt4Project *pro, const QString &proFilePath)
    : ApplicationRunConfiguration(pro),
      m_proFilePath(proFilePath),
      m_runMode(Gui),
      m_userSetName(false),
      m_configWidget(0),
      m_cachedTargetInformationValid(false),
      m_isUsingDyldImageSuffix(false),
      m_userSetWokingDirectory(false),
      m_baseEnvironmentBase(Qt4RunConfiguration::BuildEnvironmentBase)
{
    if (!m_proFilePath.isEmpty())
        setName(QFileInfo(m_proFilePath).completeBaseName());
    else
        setName(tr("Qt4RunConfiguration"));

    connect(pro, SIGNAL(activeBuildConfigurationChanged()),
            this, SLOT(invalidateCachedTargetInformation()));

    connect(pro, SIGNAL(activeBuildConfigurationChanged()),
            this, SIGNAL(baseEnvironmentChanged()));

    connect(pro, SIGNAL(environmentChanged(QString)),
            this, SIGNAL(baseEnvironmentChanged()));
}

Qt4RunConfiguration::~Qt4RunConfiguration()
{
}

QString Qt4RunConfiguration::type() const
{
    return "Qt4ProjectManager.Qt4RunConfiguration";
}


//////
/// Qt4RunConfigurationWidget
/////

Qt4RunConfigurationWidget::Qt4RunConfigurationWidget(Qt4RunConfiguration *qt4RunConfiguration, QWidget *parent)
    : QWidget(parent),
    m_qt4RunConfiguration(qt4RunConfiguration),
    m_ignoreChange(false),
    m_usingDyldImageSuffix(0),
    m_isShown(false)
{
    QFormLayout *toplayout = new QFormLayout();
    toplayout->setMargin(0);

    QLabel *nameLabel = new QLabel(tr("Name:"));
    m_nameLineEdit = new QLineEdit(m_qt4RunConfiguration->name());
    nameLabel->setBuddy(m_nameLineEdit);
    toplayout->addRow(nameLabel, m_nameLineEdit);

    m_executableLabel = new QLabel(m_qt4RunConfiguration->executable());
    toplayout->addRow(tr("Executable:"), m_executableLabel);

    m_workingDirectoryEdit = new Core::Utils::PathChooser();
    m_workingDirectoryEdit->setPath(m_qt4RunConfiguration->workingDirectory());
    m_workingDirectoryEdit->setExpectedKind(Core::Utils::PathChooser::Directory);
    m_workingDirectoryEdit->setPromptDialogTitle(tr("Select the working directory"));

    QToolButton *resetButton = new QToolButton();
    resetButton->setToolTip(tr("Reset to default"));
    resetButton->setIcon(QIcon(":/core/images/reset.png"));

    QHBoxLayout *boxlayout = new QHBoxLayout();
    boxlayout->addWidget(m_workingDirectoryEdit);
    boxlayout->addWidget(resetButton);
    toplayout->addRow(tr("Working Directory:"), boxlayout);

    QLabel *argumentsLabel = new QLabel(tr("&Arguments:"));
    m_argumentsLineEdit = new QLineEdit(ProjectExplorer::Environment::joinArgumentList(qt4RunConfiguration->commandLineArguments()));
    argumentsLabel->setBuddy(m_argumentsLineEdit);
    toplayout->addRow(argumentsLabel, m_argumentsLineEdit);

    m_useTerminalCheck = new QCheckBox(tr("Run in &Terminal"));
    m_useTerminalCheck->setChecked(m_qt4RunConfiguration->runMode() == ProjectExplorer::ApplicationRunConfiguration::Console);
    toplayout->addRow(QString(), m_useTerminalCheck);

#ifdef Q_OS_MAC
    m_usingDyldImageSuffix = new QCheckBox(tr("Use debug version of frameworks (DYLD_IMAGE_SUFFIX=_debug)"));
    m_usingDyldImageSuffix->setChecked(m_qt4RunConfiguration->isUsingDyldImageSuffix());
    toplayout->addRow(QString(), m_usingDyldImageSuffix);
    connect(m_usingDyldImageSuffix, SIGNAL(toggled(bool)),
            this, SLOT(usingDyldImageSuffixToggled(bool)));
#endif



    QGroupBox *box = new QGroupBox(tr("Environment"),this);
    QVBoxLayout *boxLayout = new QVBoxLayout();
    box->setLayout(boxLayout);
    box->setFlat(true);

    QLabel *label = new QLabel(tr("Base environment for this runconfiguration:"), this);
    boxLayout->addWidget(label);

    m_cleanEnvironmentRadioButton = new QRadioButton("Clean Environment", box);
    m_systemEnvironmentRadioButton = new QRadioButton("System Environment", box);
    m_buildEnvironmentRadioButton = new QRadioButton("Build Environment", box);
    boxLayout->addWidget(m_cleanEnvironmentRadioButton);
    boxLayout->addWidget(m_systemEnvironmentRadioButton);
    boxLayout->addWidget(m_buildEnvironmentRadioButton);

    if (qt4RunConfiguration->baseEnvironmentBase() == Qt4RunConfiguration::CleanEnvironmentBase)
        m_cleanEnvironmentRadioButton->setChecked(true);
    else if (qt4RunConfiguration->baseEnvironmentBase() == Qt4RunConfiguration::SystemEnvironmentBase)
        m_systemEnvironmentRadioButton->setChecked(true);
    else if (qt4RunConfiguration->baseEnvironmentBase() == Qt4RunConfiguration::BuildEnvironmentBase)
        m_buildEnvironmentRadioButton->setChecked(true);

    connect(m_cleanEnvironmentRadioButton, SIGNAL(toggled(bool)),
            this, SLOT(baseEnvironmentRadioButtonChanged()));
    connect(m_systemEnvironmentRadioButton, SIGNAL(toggled(bool)),
            this, SLOT(baseEnvironmentRadioButtonChanged()));
    connect(m_buildEnvironmentRadioButton, SIGNAL(toggled(bool)),
            this, SLOT(baseEnvironmentRadioButtonChanged()));

    m_environmentWidget = new ProjectExplorer::EnvironmentWidget(this);
    m_environmentWidget->setBaseEnvironment(m_qt4RunConfiguration->baseEnvironment());
    m_environmentWidget->setUserChanges(m_qt4RunConfiguration->userEnvironmentChanges());
    m_environmentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    boxLayout->addWidget(m_environmentWidget);

    QVBoxLayout *vbox = new QVBoxLayout(this);
    vbox->addLayout(toplayout);
    vbox->addWidget(box);

    connect(m_workingDirectoryEdit, SIGNAL(changed()),
            this, SLOT(setWorkingDirectory()));

    connect(resetButton, SIGNAL(clicked()),
            this, SLOT(resetWorkingDirectory()));

    connect(m_argumentsLineEdit, SIGNAL(textEdited(QString)),
            this, SLOT(setCommandLineArguments(QString)));
    connect(m_nameLineEdit, SIGNAL(textEdited(QString)),
            this, SLOT(nameEdited(QString)));
    connect(m_useTerminalCheck, SIGNAL(toggled(bool)),
            this, SLOT(termToggled(bool)));

    connect(m_environmentWidget, SIGNAL(userChangesUpdated()),
            this, SLOT(userChangesUpdated()));

    connect(qt4RunConfiguration, SIGNAL(workingDirectoryChanged(QString)),
            this, SLOT(workingDirectoryChanged(QString)));

    connect(qt4RunConfiguration, SIGNAL(commandLineArgumentsChanged(QString)),
            this, SLOT(commandLineArgumentsChanged(QString)));
    connect(qt4RunConfiguration, SIGNAL(nameChanged(QString)),
            this, SLOT(nameChanged(QString)));
    connect(qt4RunConfiguration, SIGNAL(runModeChanged(ProjectExplorer::ApplicationRunConfiguration::RunMode)),
            this, SLOT(runModeChanged(ProjectExplorer::ApplicationRunConfiguration::RunMode)));
    connect(qt4RunConfiguration, SIGNAL(usingDyldImageSuffixChanged(bool)),
            this, SLOT(usingDyldImageSuffixChanged(bool)));
    connect(qt4RunConfiguration, SIGNAL(effectiveTargetInformationChanged()),
            this, SLOT(effectiveTargetInformationChanged()), Qt::QueuedConnection);

    connect(qt4RunConfiguration, SIGNAL(userEnvironmentChangesChanged(QList<ProjectExplorer::EnvironmentItem>)),
            this, SLOT(userEnvironmentChangesChanged(QList<ProjectExplorer::EnvironmentItem>)));

    connect(qt4RunConfiguration, SIGNAL(baseEnvironmentChanged()),
            this, SLOT(baseEnvironmentChanged()));
}

void Qt4RunConfigurationWidget::baseEnvironmentRadioButtonChanged()
{
    m_ignoreChange = true;
    if (m_cleanEnvironmentRadioButton->isChecked())
        m_qt4RunConfiguration->setBaseEnvironmentBase(Qt4RunConfiguration::CleanEnvironmentBase);
    else if (m_systemEnvironmentRadioButton->isChecked())
        m_qt4RunConfiguration->setBaseEnvironmentBase(Qt4RunConfiguration::SystemEnvironmentBase);
    else if (m_buildEnvironmentRadioButton->isChecked())
        m_qt4RunConfiguration->setBaseEnvironmentBase(Qt4RunConfiguration::BuildEnvironmentBase);

    m_environmentWidget->setBaseEnvironment(m_qt4RunConfiguration->baseEnvironment());
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::baseEnvironmentChanged()
{
    if (m_ignoreChange)
        return;

    if (m_qt4RunConfiguration->baseEnvironmentBase() == Qt4RunConfiguration::CleanEnvironmentBase)
        m_cleanEnvironmentRadioButton->setChecked(true);
    else if (m_qt4RunConfiguration->baseEnvironmentBase() == Qt4RunConfiguration::SystemEnvironmentBase)
        m_systemEnvironmentRadioButton->setChecked(true);
    else if (m_qt4RunConfiguration->baseEnvironmentBase() == Qt4RunConfiguration::BuildEnvironmentBase)
        m_buildEnvironmentRadioButton->setChecked(true);

    m_environmentWidget->setBaseEnvironment(m_qt4RunConfiguration->baseEnvironment());
}

void Qt4RunConfigurationWidget::userEnvironmentChangesChanged(const QList<ProjectExplorer::EnvironmentItem> &userChanges)
{
    if (m_ignoreChange)
        return;
    m_environmentWidget->setUserChanges(userChanges);
}

void Qt4RunConfigurationWidget::userChangesUpdated()
{
    m_ignoreChange = true;
    m_qt4RunConfiguration->setUserEnvironmentChanges(m_environmentWidget->userChanges());
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::setWorkingDirectory()
{
    if (m_ignoreChange)
        return;
    m_ignoreChange = true;
    m_qt4RunConfiguration->setWorkingDirectory(m_workingDirectoryEdit->path());
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::resetWorkingDirectory()
{
    // This emits a signal connected to workingDirectoryChanged()
    // that sets the m_workingDirectoryEdit
    m_qt4RunConfiguration->setWorkingDirectory("");
}

void Qt4RunConfigurationWidget::setCommandLineArguments(const QString &args)
{
    m_ignoreChange = true;
    m_qt4RunConfiguration->setCommandLineArguments(args);
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::nameEdited(const QString &name)
{
    m_ignoreChange = true;
    m_qt4RunConfiguration->nameEdited(name);
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::termToggled(bool on)
{
    m_ignoreChange = true;
    m_qt4RunConfiguration->setRunMode(on ? ApplicationRunConfiguration::Console
                                         : ApplicationRunConfiguration::Gui);
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::usingDyldImageSuffixToggled(bool state)
{
    m_ignoreChange = true;
    m_qt4RunConfiguration->setUsingDyldImageSuffix(state);
    m_ignoreChange = false;
}

void Qt4RunConfigurationWidget::workingDirectoryChanged(const QString &workingDirectory)
{
    if (!m_ignoreChange)
        m_workingDirectoryEdit->setPath(workingDirectory);
}

void Qt4RunConfigurationWidget::commandLineArgumentsChanged(const QString &args)
{
    if (!m_ignoreChange)
        m_argumentsLineEdit->setText(args);
}

void Qt4RunConfigurationWidget::nameChanged(const QString &name)
{
    if (!m_ignoreChange)
        m_nameLineEdit->setText(name);
}

void Qt4RunConfigurationWidget::runModeChanged(ApplicationRunConfiguration::RunMode runMode)
{
    if (!m_ignoreChange)
        m_useTerminalCheck->setChecked(runMode == ApplicationRunConfiguration::Console);
}

void Qt4RunConfigurationWidget::usingDyldImageSuffixChanged(bool state)
{
    if (!m_ignoreChange && m_usingDyldImageSuffix)
        m_usingDyldImageSuffix->setChecked(state);
}

void Qt4RunConfigurationWidget::effectiveTargetInformationChanged()
{
    if (m_isShown) {
        m_executableLabel->setText(QDir::toNativeSeparators(m_qt4RunConfiguration->executable()));
        m_ignoreChange = true;
        m_workingDirectoryEdit->setPath(QDir::toNativeSeparators(m_qt4RunConfiguration->workingDirectory()));
        m_ignoreChange = false;
    }
}

void Qt4RunConfigurationWidget::showEvent(QShowEvent *event)
{
    m_isShown = true;
    effectiveTargetInformationChanged();
    QWidget::showEvent(event);
}

void Qt4RunConfigurationWidget::hideEvent(QHideEvent *event)
{
    m_isShown = false;
    QWidget::hideEvent(event);
}

////// TODO c&p above
QWidget *Qt4RunConfiguration::configurationWidget()
{
    return new Qt4RunConfigurationWidget(this, 0);
}

void Qt4RunConfiguration::save(PersistentSettingsWriter &writer) const
{
    const QDir projectDir = QFileInfo(project()->file()->fileName()).absoluteDir();
    writer.saveValue("CommandLineArguments", m_commandLineArguments);
    writer.saveValue("ProFile", projectDir.relativeFilePath(m_proFilePath));
    writer.saveValue("UserSetName", m_userSetName);
    writer.saveValue("UseTerminal", m_runMode == Console);
    writer.saveValue("UseDyldImageSuffix", m_isUsingDyldImageSuffix);
    writer.saveValue("UserEnvironmentChanges", ProjectExplorer::EnvironmentItem::toStringList(m_userEnvironmentChanges));
    writer.saveValue("BaseEnvironmentBase", m_baseEnvironmentBase);
    ApplicationRunConfiguration::save(writer);
}

void Qt4RunConfiguration::restore(const PersistentSettingsReader &reader)
{    
    ApplicationRunConfiguration::restore(reader);
    const QDir projectDir = QFileInfo(project()->file()->fileName()).absoluteDir();
    m_commandLineArguments = reader.restoreValue("CommandLineArguments").toStringList();
    m_proFilePath = projectDir.filePath(reader.restoreValue("ProFile").toString());
    m_userSetName = reader.restoreValue("UserSetName").toBool();
    m_runMode = reader.restoreValue("UseTerminal").toBool() ? Console : Gui;
    m_isUsingDyldImageSuffix = reader.restoreValue("UseDyldImageSuffix").toBool();
    if (!m_proFilePath.isEmpty()) {
        m_cachedTargetInformationValid = false;
        if (!m_userSetName)
            setName(QFileInfo(m_proFilePath).completeBaseName());
    }
    m_userEnvironmentChanges = ProjectExplorer::EnvironmentItem::fromStringList(reader.restoreValue("UserEnvironmentChanges").toStringList());
    QVariant tmp = reader.restoreValue("BaseEnvironmentBase");
    m_baseEnvironmentBase = tmp.isValid() ? BaseEnvironmentBase(tmp.toInt()) : Qt4RunConfiguration::BuildEnvironmentBase;
}

QString Qt4RunConfiguration::executable() const
{
    const_cast<Qt4RunConfiguration *>(this)->updateTarget();
    return m_executable;
}

ApplicationRunConfiguration::RunMode Qt4RunConfiguration::runMode() const
{
    return m_runMode;
}

bool Qt4RunConfiguration::isUsingDyldImageSuffix() const
{
    return m_isUsingDyldImageSuffix;
}

void Qt4RunConfiguration::setUsingDyldImageSuffix(bool state)
{
    m_isUsingDyldImageSuffix = state;
    emit usingDyldImageSuffixChanged(state);
}

QString Qt4RunConfiguration::workingDirectory() const
{
    // if the user overrode us, then return his working directory
    if (m_userSetWokingDirectory)
        return m_userWorkingDirectory;

    // else what the pro file reader tells us
    const_cast<Qt4RunConfiguration *>(this)->updateTarget();
    return m_workingDir;
}

QStringList Qt4RunConfiguration::commandLineArguments() const
{
    return m_commandLineArguments;
}

ProjectExplorer::Environment Qt4RunConfiguration::baseEnvironment() const
{
    ProjectExplorer::Environment env;
    if (m_baseEnvironmentBase == Qt4RunConfiguration::CleanEnvironmentBase) {
        // Nothing
    } else  if (m_baseEnvironmentBase == Qt4RunConfiguration::SystemEnvironmentBase) {
        env = ProjectExplorer::Environment::systemEnvironment();
    } else  if (m_baseEnvironmentBase == Qt4RunConfiguration::BuildEnvironmentBase) {
        QString config = project()->activeBuildConfiguration();
        env = project()->environment(project()->activeBuildConfiguration());
    }
    if (m_isUsingDyldImageSuffix) {
        env.set("DYLD_IMAGE_SUFFIX", "_debug");
    }
    return env;
}

ProjectExplorer::Environment Qt4RunConfiguration::environment() const
{
    ProjectExplorer::Environment env = baseEnvironment();
    env.modify(userEnvironmentChanges());
    return env;
}

QList<ProjectExplorer::EnvironmentItem> Qt4RunConfiguration::userEnvironmentChanges() const
{
    return m_userEnvironmentChanges;
}

void Qt4RunConfiguration::setUserEnvironmentChanges(const QList<ProjectExplorer::EnvironmentItem> &diff)
{
    if (m_userEnvironmentChanges != diff) {
        m_userEnvironmentChanges = diff;
        emit userEnvironmentChangesChanged(diff);
    }
}

void Qt4RunConfiguration::setWorkingDirectory(const QString &wd)
{
    if (wd== "") {
        m_userSetWokingDirectory = false;
        m_userWorkingDirectory = QString::null;
        emit workingDirectoryChanged(workingDirectory());
    } else {
        m_userSetWokingDirectory = true;
        m_userWorkingDirectory = wd;
        emit workingDirectoryChanged(m_userWorkingDirectory);
    }
}

void Qt4RunConfiguration::setCommandLineArguments(const QString &argumentsString)
{
    m_commandLineArguments = ProjectExplorer::Environment::parseCombinedArgString(argumentsString);
    emit commandLineArgumentsChanged(argumentsString);
}

void Qt4RunConfiguration::setRunMode(RunMode runMode)
{
    m_runMode = runMode;
    emit runModeChanged(runMode);
}

void Qt4RunConfiguration::nameEdited(const QString &name)
{
    if (name == "") {
        setName(tr("Qt4RunConfiguration"));
        m_userSetName = false;
    } else {
        setName(name);
        m_userSetName = true;
    }
    emit nameChanged(name);
}

QString Qt4RunConfiguration::proFilePath() const
{
    return m_proFilePath;
}

void Qt4RunConfiguration::updateTarget()
{
    if (m_cachedTargetInformationValid)
        return;
    //qDebug()<<"updateTarget";
    Qt4Project *pro = static_cast<Qt4Project *>(project());
    Qt4PriFileNode * priFileNode = static_cast<Qt4Project *>(project())->rootProjectNode()->findProFileFor(m_proFilePath);
    if (!priFileNode) {
        m_workingDir = QString::null;
        m_executable = QString::null;
        m_cachedTargetInformationValid = true;
        emit effectiveTargetInformationChanged();
        return;
    }
    ProFileReader *reader = priFileNode->createProFileReader();
    reader->setCumulative(false);
    reader->setQtVersion(pro->qtVersion(pro->activeBuildConfiguration()));

    // Find out what flags we pass on to qmake, this code is duplicated in the qmake step
    QtVersion::QmakeBuildConfig defaultBuildConfiguration = pro->qtVersion(pro->activeBuildConfiguration())->defaultBuildConfig();
    QtVersion::QmakeBuildConfig projectBuildConfiguration = QtVersion::QmakeBuildConfig(pro->qmakeStep()->value(pro->activeBuildConfiguration(), "buildConfiguration").toInt());
    QStringList addedUserConfigArguments;
    QStringList removedUserConfigArguments;
    if ((defaultBuildConfiguration & QtVersion::BuildAll) && !(projectBuildConfiguration & QtVersion::BuildAll))
        removedUserConfigArguments << "debug_and_release";
    if (!(defaultBuildConfiguration & QtVersion::BuildAll) && (projectBuildConfiguration & QtVersion::BuildAll))
        addedUserConfigArguments << "debug_and_release";
    if ((defaultBuildConfiguration & QtVersion::DebugBuild) && !(projectBuildConfiguration & QtVersion::DebugBuild))
        addedUserConfigArguments << "release";
    if (!(defaultBuildConfiguration & QtVersion::DebugBuild) && (projectBuildConfiguration & QtVersion::DebugBuild))
        addedUserConfigArguments << "debug";

    reader->setUserConfigCmdArgs(addedUserConfigArguments, removedUserConfigArguments);

    if (!reader->readProFile(m_proFilePath)) {
        delete reader;
        Core::ICore::instance()->messageManager()->printToOutputPane(tr("Could not parse %1. The Qt4 run configuration %2 can not be started.").arg(m_proFilePath).arg(name()));
        return;
    }

    // Extract data
    QDir baseProjectDirectory = QFileInfo(project()->file()->fileName()).absoluteDir();
    QString relSubDir = baseProjectDirectory.relativeFilePath(QFileInfo(m_proFilePath).path());
    QDir baseBuildDirectory = project()->buildDirectory(project()->activeBuildConfiguration());
    QString baseDir = baseBuildDirectory.absoluteFilePath(relSubDir);

    //qDebug()<<relSubDir<<baseDir;

    // Working Directory
    if (reader->contains("DESTDIR")) {
        //qDebug()<<"reader contains destdir:"<<reader->value("DESTDIR");
        m_workingDir = reader->value("DESTDIR");
        if (QDir::isRelativePath(m_workingDir)) {
            m_workingDir = baseDir + QLatin1Char('/') + m_workingDir;
            //qDebug()<<"was relative and expanded to"<<m_workingDir;
        }
    } else {
        //qDebug()<<"reader didn't contain DESTDIR, setting to "<<baseDir;
        m_workingDir = baseDir;
        if (reader->values("CONFIG").contains("debug_and_release")) {
            QString qmakeBuildConfig = "release";
            if (projectBuildConfiguration & QtVersion::DebugBuild)
                qmakeBuildConfig = "debug";
            if (!reader->contains("DESTDIR"))
                m_workingDir += QLatin1Char('/') + qmakeBuildConfig;
        }
    }

#if defined (Q_OS_MAC)
    if (reader->values("CONFIG").contains("app_bundle")) {
        m_workingDir += QLatin1Char('/')
                   + reader->value("TARGET")
                   + QLatin1String(".app/Contents/MacOS");
    }
#endif

    m_workingDir = QDir::cleanPath(m_workingDir);
    m_executable = QDir::cleanPath(m_workingDir + QLatin1Char('/') + reader->value("TARGET"));
    //qDebug()<<"##### updateTarget sets:"<<m_workingDir<<m_executable;

#if defined (Q_OS_WIN)
    m_executable += QLatin1String(".exe");
#endif

    delete reader;

    m_cachedTargetInformationValid = true;

    emit effectiveTargetInformationChanged();
}

void Qt4RunConfiguration::invalidateCachedTargetInformation()
{
    m_cachedTargetInformationValid = false;
    emit effectiveTargetInformationChanged();
}

QString Qt4RunConfiguration::dumperLibrary() const
{
    Qt4Project *pro = qobject_cast<Qt4Project *>(project());
    QtVersion *version = pro->qtVersion(pro->activeBuildConfiguration());
    return version->debuggingHelperLibrary();
}

void Qt4RunConfiguration::setBaseEnvironmentBase(BaseEnvironmentBase env)
{
    if (m_baseEnvironmentBase == env)
        return;
    m_baseEnvironmentBase = env;
    emit baseEnvironmentChanged();
}

Qt4RunConfiguration::BaseEnvironmentBase Qt4RunConfiguration::baseEnvironmentBase() const
{
    return m_baseEnvironmentBase;
}
ProjectExplorer::ToolChain::ToolChainType Qt4RunConfiguration::toolChainType() const
{
    Qt4Project *pro = qobject_cast<Qt4Project *>(project());
    return pro->toolChainType(pro->activeBuildConfiguration());
}

///
/// Qt4RunConfigurationFactory
/// This class is used to restore run settings (saved in .user files)
///

Qt4RunConfigurationFactory::Qt4RunConfigurationFactory()
{
}

Qt4RunConfigurationFactory::~Qt4RunConfigurationFactory()
{
}

// used to recreate the runConfigurations when restoring settings
bool Qt4RunConfigurationFactory::canRestore(const QString &type) const
{
    return type == "Qt4ProjectManager.Qt4RunConfiguration";
}

QSharedPointer<ProjectExplorer::RunConfiguration> Qt4RunConfigurationFactory::create
    (ProjectExplorer::Project *project, const QString &type)
{
    Qt4Project *p = qobject_cast<Qt4Project *>(project);
    Q_ASSERT(p);
    if (type.startsWith("Qt4RunConfiguration.")) {
        QString fileName = type.mid(QString("Qt4RunConfiguration.").size());
        return QSharedPointer<ProjectExplorer::RunConfiguration>(new Qt4RunConfiguration(p, fileName));
    }
    Q_ASSERT(type == "Qt4ProjectManager.Qt4RunConfiguration");
    // The right path is set in restoreSettings
    QSharedPointer<ProjectExplorer::RunConfiguration> rc(new Qt4RunConfiguration(p, QString::null));
    return rc;
}

QStringList Qt4RunConfigurationFactory::availableCreationTypes(ProjectExplorer::Project *pro) const
{
    Qt4Project *qt4project = qobject_cast<Qt4Project *>(pro);
    if (qt4project) {
        QStringList applicationProFiles;
        QList<Qt4ProFileNode *> list = qt4project->applicationProFiles();
        foreach (Qt4ProFileNode * node, list) {
            applicationProFiles.append("Qt4RunConfiguration." + node->path());
        }
        return applicationProFiles;
    } else {
        return QStringList();
    }
}

QString Qt4RunConfigurationFactory::displayNameForType(const QString &type) const
{
    QString fileName = type.mid(QString("Qt4RunConfiguration.").size());
    return QFileInfo(fileName).completeBaseName();
}
