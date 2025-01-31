// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qaxwidgettaskmenu.h"
#include "qdesigneraxwidget.h"
#include "qaxwidgetpropertysheet.h"

#include <QtDesigner/abstractformwindow.h>
#include <QtDesigner/abstractformwindowcursor.h>
#include <QtDesigner/abstractformeditor.h>
#include <QtDesigner/qextensionmanager.h>

#include <QtAxContainer/qaxselect.h>

#include <QtWidgets/qmessagebox.h>
#include <QtGui/qundostack.h>

#include <QtGui/qaction.h>

#include <QtCore/qt_windows.h>
#include <QtCore/quuid.h>

#include <olectl.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

/* SetControlCommand: An undo commands that sets a control bypassing
   Designer's property system which cannot handle the changing
   of the 'control' property's index and other cached information
   when modifying it. */

class SetControlCommand : public QUndoCommand
{
public:
    SetControlCommand(QDesignerAxWidget *ax, QDesignerFormWindowInterface *core, const QString &newClsid = QString());

    virtual void redo() override {  apply(m_newClsid); }
    virtual void undo() override {  apply(m_oldClsid);  }

private:
    bool apply(const QString &clsid);

    QDesignerAxWidget *m_axWidget;
    QDesignerFormWindowInterface *m_formWindow;
    QString m_oldClsid;
    QString m_newClsid;
};

SetControlCommand::SetControlCommand(QDesignerAxWidget *ax, QDesignerFormWindowInterface *fw, const QString &newClsid) :
    m_axWidget(ax),
    m_formWindow(fw),
    m_oldClsid(ax->control()),
    m_newClsid(newClsid)
{
    if (m_newClsid.isEmpty())
        setText(QDesignerAxWidget::tr("Reset control"));
    else
        setText(QDesignerAxWidget::tr("Set control"));
}

bool SetControlCommand::apply(const QString &clsid)
{
    if (m_oldClsid == m_newClsid)
        return true;

    QObject *ext = m_formWindow->core()->extensionManager()->extension(
            m_axWidget, Q_TYPEID(QDesignerPropertySheetExtension));
    auto sheet = qobject_cast<QAxWidgetPropertySheet *>(ext);
    if (!sheet)
        return false;

    const bool hasClsid = !clsid.isEmpty();
    const int index = sheet->indexOf(QLatin1String(QAxWidgetPropertySheet::controlPropertyName));
    if (hasClsid)
        sheet->setProperty(index, clsid);
    else
        sheet->reset(index);
    return true;
}

// -------------------- QAxWidgetTaskMenu
QAxWidgetTaskMenu::QAxWidgetTaskMenu(QDesignerAxWidget *object, QObject *parent) :
    QObject(parent),
    m_axwidget(object),
    m_setAction(new QAction(tr("Set Control"), this)),
    m_resetAction(new QAction(tr("Reset Control"), this))
{
    connect(m_setAction, &QAction::triggered, this, &QAxWidgetTaskMenu::setActiveXControl);
    connect(m_resetAction, &QAction::triggered, this, &QAxWidgetTaskMenu::resetActiveXControl);
    m_taskActions.push_back(m_setAction);
    m_taskActions.push_back(m_resetAction);
}

QAxWidgetTaskMenu::~QAxWidgetTaskMenu() = default;

QList<QAction*> QAxWidgetTaskMenu::taskActions() const
{
    const bool loaded = m_axwidget->loaded();
    m_setAction->setEnabled(!loaded);
    m_resetAction->setEnabled(loaded);
    return m_taskActions;
}

void QAxWidgetTaskMenu::resetActiveXControl()
{
    auto formWin = QDesignerFormWindowInterface::findFormWindow(m_axwidget);
    Q_ASSERT(formWin != nullptr);
    formWin->commandHistory()->push(new SetControlCommand(m_axwidget, formWin));
}

void QAxWidgetTaskMenu::setActiveXControl()
{
    QAxSelect dialog(m_axwidget->topLevelWidget());
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto clsid = QUuid::fromString(dialog.clsid());
    QString key;

    IClassFactory2 *cf2 = nullptr;
    CoGetClassObject(clsid, CLSCTX_SERVER, 0, IID_IClassFactory2, reinterpret_cast<void **>(&cf2));

    if (cf2) {
        BSTR bKey;
        HRESULT hres = cf2->RequestLicKey(0, &bKey);
        if (hres == CLASS_E_NOTLICENSED) {
            QMessageBox::warning(m_axwidget->topLevelWidget(), tr("Licensed Control"),
                                 tr("The control requires a design-time license"));
            cf2->Release();
            return;
        }

        key = QString::fromWCharArray(bKey);
        cf2->Release();
    }

    auto formWin = QDesignerFormWindowInterface::findFormWindow(m_axwidget);

    Q_ASSERT(formWin != nullptr);
    QString value = clsid.toString();
    if (!key.isEmpty())
        value += u':' + key;
    formWin->commandHistory()->push(new SetControlCommand(m_axwidget, formWin, value));
}

QT_END_NAMESPACE
