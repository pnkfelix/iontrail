/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */
#include "nsIDOMHTMLIFrameElement.h"
#include "nsIScriptObjectOwner.h"
#include "nsIDOMEventReceiver.h"
#include "nsIHTMLContent.h"
#include "nsGenericHTMLElement.h"
#include "nsHTMLAtoms.h"
#include "nsHTMLIIDs.h"
#include "nsIStyleContext.h"
#include "nsIMutableStyleContext.h"
#include "nsStyleConsts.h"
#include "nsIPresContext.h"
#include "nsIPresShell.h"
#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsIWebNavigation.h"
#include "nsIHTMLAttributes.h"
#include "nsIChromeEventHandler.h"
#include "nsDOMError.h"

static NS_DEFINE_IID(kIDOMHTMLIFrameElementIID, NS_IDOMHTMLIFRAMEELEMENT_IID);

class nsHTMLIFrameElement : public nsIDOMHTMLIFrameElement,
                            public nsIJSScriptObject,
                            public nsIHTMLContent,
                            public nsIChromeEventHandler
{
public:
  nsHTMLIFrameElement(nsINodeInfo *aNodeInfo);
  virtual ~nsHTMLIFrameElement();

  // nsISupports
  NS_DECL_ISUPPORTS

  // nsIDOMNode
  NS_IMPL_IDOMNODE_USING_GENERIC(mInner)

  // nsIDOMElement
  NS_IMPL_IDOMELEMENT_USING_GENERIC(mInner)

  // nsIDOMHTMLElement
  NS_IMPL_IDOMHTMLELEMENT_USING_GENERIC(mInner)

  // nsIDOMHTMLIFrameElement
  NS_IMETHOD GetAlign(nsString& aAlign);
  NS_IMETHOD SetAlign(const nsString& aAlign);
  NS_IMETHOD GetFrameBorder(nsString& aFrameBorder);
  NS_IMETHOD SetFrameBorder(const nsString& aFrameBorder);
  NS_IMETHOD GetHeight(nsString& aHeight);
  NS_IMETHOD SetHeight(const nsString& aHeight);
  NS_IMETHOD GetLongDesc(nsString& aLongDesc);
  NS_IMETHOD SetLongDesc(const nsString& aLongDesc);
  NS_IMETHOD GetMarginHeight(nsString& aMarginHeight);
  NS_IMETHOD SetMarginHeight(const nsString& aMarginHeight);
  NS_IMETHOD GetMarginWidth(nsString& aMarginWidth);
  NS_IMETHOD SetMarginWidth(const nsString& aMarginWidth);
  NS_IMETHOD GetName(nsString& aName);
  NS_IMETHOD SetName(const nsString& aName);
  NS_IMETHOD GetScrolling(nsString& aScrolling);
  NS_IMETHOD SetScrolling(const nsString& aScrolling);
  NS_IMETHOD GetSrc(nsString& aSrc);
  NS_IMETHOD SetSrc(const nsString& aSrc);
  NS_IMETHOD GetWidth(nsString& aWidth);
  NS_IMETHOD SetWidth(const nsString& aWidth);
  NS_IMETHOD GetContentDocument(nsIDOMDocument** aContentDocument);
  NS_IMETHOD SetContentDocument(nsIDOMDocument* aContentDocument);

  // nsIJSScriptObject
  NS_IMPL_IJSSCRIPTOBJECT_USING_GENERIC(mInner)

  // nsIContent
  NS_IMPL_ICONTENT_USING_GENERIC(mInner)

  // nsIHTMLContent
  NS_IMPL_IHTMLCONTENT_USING_GENERIC(mInner)

  // nsIChromeEventHandler
  NS_DECL_NSICHROMEEVENTHANDLER

protected:
  nsGenericHTMLContainerElement mInner;
};

nsresult
NS_NewHTMLIFrameElement(nsIHTMLContent** aInstancePtrResult,
                        nsINodeInfo *aNodeInfo)
{
  NS_ENSURE_ARG_POINTER(aInstancePtrResult);
  NS_ENSURE_ARG_POINTER(aNodeInfo);

  nsIHTMLContent* it = new nsHTMLIFrameElement(aNodeInfo);
  if (nsnull == it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return it->QueryInterface(kIHTMLContentIID, (void**) aInstancePtrResult);
}


nsHTMLIFrameElement::nsHTMLIFrameElement(nsINodeInfo *aNodeInfo)
{
  NS_INIT_REFCNT();
  mInner.Init(this, aNodeInfo);
}

nsHTMLIFrameElement::~nsHTMLIFrameElement()
{
}

NS_IMPL_ADDREF(nsHTMLIFrameElement)

NS_IMPL_RELEASE(nsHTMLIFrameElement)

nsresult
nsHTMLIFrameElement::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
  NS_IMPL_HTML_CONTENT_QUERY_INTERFACE(aIID, aInstancePtr, this)
  if (aIID.Equals(kIDOMHTMLIFrameElementIID)) {
    nsIDOMHTMLIFrameElement* tmp = this;
    *aInstancePtr = (void*) tmp;
    NS_ADDREF_THIS();
    return NS_OK;
  }
  else if (aIID.Equals(NS_GET_IID(nsIChromeEventHandler))) {
    *aInstancePtr = NS_STATIC_CAST(nsIChromeEventHandler*, this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  return NS_NOINTERFACE;
}

nsresult
nsHTMLIFrameElement::CloneNode(PRBool aDeep, nsIDOMNode** aReturn)
{
  nsHTMLIFrameElement* it = new nsHTMLIFrameElement(mInner.mNodeInfo);
  if (nsnull == it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mInner.CopyInnerTo(this, &it->mInner, aDeep);
  return it->QueryInterface(kIDOMNodeIID, (void**) aReturn);
}

NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, Align, align)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, FrameBorder, frameborder)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, Height, height)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, LongDesc, longdesc)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, MarginHeight, marginheight)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, MarginWidth, marginwidth)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, Name, name)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, Scrolling, scrolling)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, Src, src)
NS_IMPL_STRING_ATTR(nsHTMLIFrameElement, Width, width)


NS_IMETHODIMP
nsHTMLIFrameElement::GetContentDocument(nsIDOMDocument** aContentDocument)
{
  NS_ENSURE_ARG_POINTER(aContentDocument);

  *aContentDocument = nsnull;

  NS_ENSURE_TRUE(mInner.mDocument, NS_OK);

  nsCOMPtr<nsIPresShell> presShell;

  presShell = dont_AddRef(mInner.mDocument->GetShellAt(0));
  NS_ENSURE_TRUE(presShell, NS_OK);

  nsCOMPtr<nsISupports> tmp;

  presShell->GetSubShellFor(this, getter_AddRefs(tmp));
  NS_ENSURE_TRUE(tmp, NS_OK);

  nsCOMPtr<nsIWebNavigation> webNav = do_QueryInterface(tmp);
  NS_ENSURE_TRUE(webNav, NS_OK);

  nsCOMPtr<nsIDOMDocument> domDoc;

  webNav->GetDocument(getter_AddRefs(domDoc));

  *aContentDocument = domDoc;

  NS_IF_ADDREF(*aContentDocument);

  return NS_OK;
}
  
NS_IMETHODIMP
nsHTMLIFrameElement::SetContentDocument(nsIDOMDocument* aContentDocument)
{   
  return NS_ERROR_DOM_INVALID_MODIFICATION_ERR;
} 
    
NS_IMETHODIMP
nsHTMLIFrameElement::StringToAttribute(nsIAtom* aAttribute,
                                       const nsString& aValue,
                                       nsHTMLValue& aResult)
{
  if (aAttribute == nsHTMLAtoms::marginwidth) {
    if (nsGenericHTMLElement::ParseValueOrPercent(aValue, aResult,
                                                  eHTMLUnit_Pixel)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  } 
  else if (aAttribute == nsHTMLAtoms::marginheight) {
    if (nsGenericHTMLElement::ParseValueOrPercent(aValue, aResult,
                                                  eHTMLUnit_Pixel)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  } 
  if (aAttribute == nsHTMLAtoms::width) {
    if (nsGenericHTMLElement::ParseValueOrPercent(aValue, aResult,
                                                  eHTMLUnit_Pixel)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  } 
  else if (aAttribute == nsHTMLAtoms::height) {
    if (nsGenericHTMLElement::ParseValueOrPercent(aValue, aResult,
                                                  eHTMLUnit_Pixel)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  } 
  else if (aAttribute == nsHTMLAtoms::frameborder) {
    if (nsGenericHTMLElement::ParseFrameborderValue(PR_TRUE, aValue, aResult)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  } 
  else if (aAttribute == nsHTMLAtoms::scrolling) {
    if (nsGenericHTMLElement::ParseScrollingValue(PR_TRUE, aValue, aResult)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  }
  else if (aAttribute == nsHTMLAtoms::align) {
    if (nsGenericHTMLElement::ParseAlignValue(aValue, aResult)) {
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  }
  return NS_CONTENT_ATTR_NOT_THERE;
}

NS_IMETHODIMP
nsHTMLIFrameElement::AttributeToString(nsIAtom* aAttribute,
                                       const nsHTMLValue& aValue,
                                       nsString& aResult) const
{
  if (aAttribute == nsHTMLAtoms::frameborder) {
    nsGenericHTMLElement::FrameborderValueToString(PR_TRUE, aValue, aResult);
    return NS_CONTENT_ATTR_HAS_VALUE;
  } 
  else if (aAttribute == nsHTMLAtoms::scrolling) {
    nsGenericHTMLElement::ScrollingValueToString(PR_TRUE, aValue, aResult);
    return NS_CONTENT_ATTR_HAS_VALUE;
  }
  else if (aAttribute == nsHTMLAtoms::align) {
    if (eHTMLUnit_Enumerated == aValue.GetUnit()) {
      nsGenericHTMLElement::AlignValueToString(aValue, aResult);
      return NS_CONTENT_ATTR_HAS_VALUE;
    }
  }
  return mInner.AttributeToString(aAttribute, aValue, aResult);
}

static void
MapAttributesInto(const nsIHTMLMappedAttributes* aAttributes,
                  nsIMutableStyleContext* aContext,
                  nsIPresContext* aPresContext)
{
  if (nsnull != aAttributes) {
    nsGenericHTMLElement::MapImageAlignAttributeInto(aAttributes, aContext, aPresContext);

    nsHTMLValue value;

    float p2t;
    aPresContext->GetScaledPixelsToTwips(&p2t);
    nsStylePosition* pos = (nsStylePosition*)
      aContext->GetMutableStyleData(eStyleStruct_Position);

    // width: value
    aAttributes->GetAttribute(nsHTMLAtoms::width, value);
    if (value.GetUnit() == eHTMLUnit_Pixel) {
      nscoord twips = NSIntPixelsToTwips(value.GetPixelValue(), p2t);
      pos->mWidth.SetCoordValue(twips);
    }
    else if (value.GetUnit() == eHTMLUnit_Percent) {
      pos->mWidth.SetPercentValue(value.GetPercentValue());
    }

    // height: value
    aAttributes->GetAttribute(nsHTMLAtoms::height, value);
    if (value.GetUnit() == eHTMLUnit_Pixel) {
      nscoord twips = NSIntPixelsToTwips(value.GetPixelValue(), p2t);
      pos->mHeight.SetCoordValue(twips);
    }
    else if (value.GetUnit() == eHTMLUnit_Percent) {
      pos->mHeight.SetPercentValue(value.GetPercentValue());
    }
  }
  nsGenericHTMLElement::MapCommonAttributesInto(aAttributes, aContext, aPresContext);
}

NS_IMETHODIMP
nsHTMLIFrameElement::GetMappedAttributeImpact(const nsIAtom* aAttribute,
                                              PRInt32& aHint) const
{
  if ((aAttribute == nsHTMLAtoms::width) ||
      (aAttribute == nsHTMLAtoms::height)) {
    aHint = NS_STYLE_HINT_REFLOW;
  }
  else if (aAttribute == nsHTMLAtoms::align) {
    aHint = NS_STYLE_HINT_REFLOW;
  }
  else if (! nsGenericHTMLElement::GetCommonMappedAttributesImpact(aAttribute, aHint)) {
    aHint = NS_STYLE_HINT_CONTENT;
  }

  return NS_OK;
}



NS_IMETHODIMP
nsHTMLIFrameElement::GetAttributeMappingFunctions(nsMapAttributesFunc& aFontMapFunc,
                                                  nsMapAttributesFunc& aMapFunc) const
{
  aFontMapFunc = nsnull;
  aMapFunc = &MapAttributesInto;
  return NS_OK;
}


NS_IMETHODIMP
nsHTMLIFrameElement::HandleDOMEvent(nsIPresContext* aPresContext,
                                    nsEvent* aEvent,
                                    nsIDOMEvent** aDOMEvent,
                                    PRUint32 aFlags,
                                    nsEventStatus* aEventStatus)
{
  return mInner.HandleDOMEvent(aPresContext, aEvent, aDOMEvent,
                               aFlags, aEventStatus);
}


NS_IMETHODIMP
nsHTMLIFrameElement::SizeOf(nsISizeOfHandler* aSizer, PRUint32* aResult) const
{
  return mInner.SizeOf(aSizer, aResult, sizeof(*this));
}

//*****************************************************************************
// nsHTMLIFrameElement::nsIChromeEventHandler
//*****************************************************************************   

NS_IMETHODIMP nsHTMLIFrameElement::HandleChromeEvent(nsIPresContext* aPresContext,
   nsEvent* aEvent, nsIDOMEvent** aDOMEvent, PRUint32 aFlags, 
   nsEventStatus* aEventStatus)
{
   return HandleDOMEvent(aPresContext, aEvent, aDOMEvent, aFlags,aEventStatus);
}
