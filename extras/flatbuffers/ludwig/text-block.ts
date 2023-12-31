// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import { FbVoid } from '../ludwig/fb-void.ts';
import { TextBlocks } from '../ludwig/text-blocks.ts';
import { TextCodeBlock } from '../ludwig/text-code-block.ts';
import { TextImage } from '../ludwig/text-image.ts';
import { TextList } from '../ludwig/text-list.ts';
import { TextSpans } from '../ludwig/text-spans.ts';


export enum TextBlock {
  NONE = 0,
  P = 1,
  H1 = 2,
  H2 = 3,
  H3 = 4,
  H4 = 5,
  H5 = 6,
  H6 = 7,
  Blockquote = 8,
  Code = 9,
  Math = 10,
  Image = 11,
  Hr = 12,
  UnorderedList = 13,
  OrderedList = 14
}

export function unionToTextBlock(
  type: TextBlock,
  accessor: (obj:FbVoid|TextBlocks|TextCodeBlock|TextImage|TextList|TextSpans|string) => FbVoid|TextBlocks|TextCodeBlock|TextImage|TextList|TextSpans|string|null
): FbVoid|TextBlocks|TextCodeBlock|TextImage|TextList|TextSpans|string|null {
  switch(TextBlock[type]) {
    case 'NONE': return null; 
    case 'P': return accessor(new TextSpans())! as TextSpans;
    case 'H1': return accessor(new TextSpans())! as TextSpans;
    case 'H2': return accessor(new TextSpans())! as TextSpans;
    case 'H3': return accessor(new TextSpans())! as TextSpans;
    case 'H4': return accessor(new TextSpans())! as TextSpans;
    case 'H5': return accessor(new TextSpans())! as TextSpans;
    case 'H6': return accessor(new TextSpans())! as TextSpans;
    case 'Blockquote': return accessor(new TextBlocks())! as TextBlocks;
    case 'Code': return accessor(new TextCodeBlock())! as TextCodeBlock;
    case 'Math': return accessor('') as string;
    case 'Image': return accessor(new TextImage())! as TextImage;
    case 'Hr': return accessor(new FbVoid())! as FbVoid;
    case 'UnorderedList': return accessor(new TextList())! as TextList;
    case 'OrderedList': return accessor(new TextList())! as TextList;
    default: return null;
  }
}

export function unionListToTextBlock(
  type: TextBlock, 
  accessor: (index: number, obj:FbVoid|TextBlocks|TextCodeBlock|TextImage|TextList|TextSpans|string) => FbVoid|TextBlocks|TextCodeBlock|TextImage|TextList|TextSpans|string|null, 
  index: number
): FbVoid|TextBlocks|TextCodeBlock|TextImage|TextList|TextSpans|string|null {
  switch(TextBlock[type]) {
    case 'NONE': return null; 
    case 'P': return accessor(index, new TextSpans())! as TextSpans;
    case 'H1': return accessor(index, new TextSpans())! as TextSpans;
    case 'H2': return accessor(index, new TextSpans())! as TextSpans;
    case 'H3': return accessor(index, new TextSpans())! as TextSpans;
    case 'H4': return accessor(index, new TextSpans())! as TextSpans;
    case 'H5': return accessor(index, new TextSpans())! as TextSpans;
    case 'H6': return accessor(index, new TextSpans())! as TextSpans;
    case 'Blockquote': return accessor(index, new TextBlocks())! as TextBlocks;
    case 'Code': return accessor(index, new TextCodeBlock())! as TextCodeBlock;
    case 'Math': return accessor(index, '') as string;
    case 'Image': return accessor(index, new TextImage())! as TextImage;
    case 'Hr': return accessor(index, new FbVoid())! as FbVoid;
    case 'UnorderedList': return accessor(index, new TextList())! as TextList;
    case 'OrderedList': return accessor(index, new TextList())! as TextList;
    default: return null;
  }
}
