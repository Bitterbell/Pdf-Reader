/* Copyright 2015 the SumatraPDF project authors (see AUTHORS file).
License: GPLv3 */

/*
Info on drag & drop:
http://www.catch22.net/tuts/drop-target
http://www.codeproject.com/Articles/814/A-generic-IDropTarget-COM-class-for-dropped-text
http://www.codeproject.com/Tips/127813/Using-SetCapture-and-ReleaseCapture-correctly-usua
http://www.codeproject.com/Articles/821/The-Drag-and-Drop-interface-samples

http://blogs.msdn.com/b/oldnewthing/archive/2008/03/11/8080077.aspx - What a drag: Dragging text
http://blogs.msdn.com/b/oldnewthing/archive/2008/03/12/8080101.aspx - What a drag: Dragging a Uniform Resource Locator (URL)
http://blogs.msdn.com/b/oldnewthing/archive/2008/03/13/8080135.aspx - What a drag: Dragging a Uniform Resource Locator (URL) and text

http://blogs.msdn.com/b/oldnewthing/archive/2004/12/06/275659.aspx - Dragging a shell object, part 1: Getting the IDataObject
http://blogs.msdn.com/b/oldnewthing/archive/2004/12/07/277581.aspx - Dragging a shell object, part 2: Enabling the Move operation
http://blogs.msdn.com/b/oldnewthing/archive/2004/12/08/278295.aspx - Dragging a shell object, part 3: Detecting an optimized move
http://blogs.msdn.com/b/oldnewthing/archive/2004/12/09/278914.aspx - Dragging a shell object, part 4: Adding a prettier drag icon
http://blogs.msdn.com/b/oldnewthing/archive/2004/12/10/279530.aspx - Dragging a shell object, part 5: Making somebody else do the heavy lifting
    
http://blogs.msdn.com/b/oldnewthing/archive/2008/03/18/8080183.aspx - What a drag: Dragging a virtual file (HGLOBAL edition)
http://blogs.msdn.com/b/oldnewthing/archive/2008/03/19/8080215.aspx - What a drag: Dragging a virtual file (IStream edition)
http://blogs.msdn.com/b/oldnewthing/archive/2008/03/20/8080229.aspx - What a drag: Dragging a virtual file (IStorage edition)
http://blogs.msdn.com/b/oldnewthing/archive/2008/03/31/8344798.aspx - You can drag multiple virtual objects, you know

http://blogs.msdn.com/b/oldnewthing/archive/tags/what+a+drag/default.aspx

http://blogs.msdn.com/b/oldnewthing/archive/2010/05/03/10006065.aspx - accept files to be opened via IDropTarget instead of on the command line?
http://blogs.msdn.com/b/oldnewthing/archive/2010/03/04/9972520.aspx - What happens if I drag the mouse by exactly the amount specified by SM_CXDRAG?
http://blogs.msdn.com/b/oldnewthing/archive/2008/07/24/8768095.aspx - Reading a contract from the other side: Simulating a drop

http://blogs.msdn.com/b/oldnewthing/archive/2011/12/28/10251521.aspx - Using the MNS_DRAGDROP style: Dragging out
http://blogs.msdn.com/b/oldnewthing/archive/2011/12/30/10251751.aspx - Using the MNS_DRAGDROP style: Menu rearrangement


http://stackoverflow.com/questions/8602317/trying-to-make-an-application-can-attach-and-detach-a-tab-from-window-by-drag-an

*/

#include "BaseUtil.h"

#include "DragDrop.h"

HRESULT DropSource::QueryInterface(REFIID riid, void **ppv) {
    IUnknown *punk = NULL;
    if (riid == IID_IUnknown) {
        punk = static_cast<IUnknown*>(this);
    } else if (riid == IID_IDropSource) {
        punk = static_cast<IDropSource*>(this);
    }

    *ppv = punk;
    if (punk) {
        punk->AddRef();
        return S_OK;
    } else {
        return E_NOINTERFACE;
    }
}

ULONG DropSource::AddRef() {
    return ++refCount;
}

ULONG DropSource::Release() {
    ULONG cRef = --refCount;
    if (cRef == 0) delete this;
    return cRef;
}

HRESULT DropSource::QueryContinueDrag(
    BOOL fEscapePressed, DWORD grfKeyState) {
    if (fEscapePressed) return DRAGDROP_S_CANCEL;

    // [Update: missing paren repaired, 7 Dec]
    if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON)))
        return DRAGDROP_S_DROP;

    return S_OK;
}

HRESULT DropSource::GiveFeedback(DWORD dwEffect) {
    UNUSED(dwEffect);
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

HRESULT DropTarget::QueryInterface(REFIID riid, void **ppv) {
    IUnknown *punk = NULL;
    if (riid == IID_IUnknown) {
        punk = static_cast<IUnknown*>(this);
    } else if (riid == IID_IDropTarget) {
        punk = static_cast<IDropTarget*>(this);
    }

    *ppv = punk;
    if (punk) {
        punk->AddRef();
        return S_OK;
    } else {
        return E_NOINTERFACE;
    }
}

ULONG DropTarget::AddRef() {
    return ++refCount;
}

ULONG DropTarget::Release() {
    ULONG cRef = --refCount;
    if (cRef == 0) delete this;
    return cRef;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragEnter(
    /* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
    /* [in] */ DWORD grfKeyState,
    /* [in] */ POINTL pt,
    /* [out][in] */ __RPC__inout DWORD *pdwEffect) {
    UNUSED(pDataObj);
    UNUSED(grfKeyState);
    UNUSED(pt);
    UNUSED(pdwEffect);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragOver(
    /* [in] */ DWORD grfKeyState,
    /* [in] */ POINTL pt,
    /* [out][in] */ __RPC__inout DWORD *pdwEffect) {
    UNUSED(grfKeyState);
    UNUSED(pt);
    UNUSED(pdwEffect);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragLeave(void) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::Drop(
    /* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
    /* [in] */ DWORD grfKeyState,
    /* [in] */ POINTL pt,
    /* [out][in] */ __RPC__inout DWORD *pdwEffect) {
    UNUSED(pDataObj);
    UNUSED(grfKeyState);
    UNUSED(pt);
    UNUSED(pdwEffect);
    return S_OK;
}

