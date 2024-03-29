// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import { ModActionEdit } from '../ludwig/mod-action-edit.ts';
import { ModActionPurge } from '../ludwig/mod-action-purge.ts';
import { ModActionSetState } from '../ludwig/mod-action-set-state.ts';


export enum ModAction {
  NONE = 0,
  ModActionEdit = 1,
  ModActionSetState = 2,
  ModActionPurge = 3
}

export function unionToModAction(
  type: ModAction,
  accessor: (obj:ModActionEdit|ModActionPurge|ModActionSetState) => ModActionEdit|ModActionPurge|ModActionSetState|null
): ModActionEdit|ModActionPurge|ModActionSetState|null {
  switch(ModAction[type]) {
    case 'NONE': return null; 
    case 'ModActionEdit': return accessor(new ModActionEdit())! as ModActionEdit;
    case 'ModActionSetState': return accessor(new ModActionSetState())! as ModActionSetState;
    case 'ModActionPurge': return accessor(new ModActionPurge())! as ModActionPurge;
    default: return null;
  }
}

export function unionListToModAction(
  type: ModAction, 
  accessor: (index: number, obj:ModActionEdit|ModActionPurge|ModActionSetState) => ModActionEdit|ModActionPurge|ModActionSetState|null, 
  index: number
): ModActionEdit|ModActionPurge|ModActionSetState|null {
  switch(ModAction[type]) {
    case 'NONE': return null; 
    case 'ModActionEdit': return accessor(index, new ModActionEdit())! as ModActionEdit;
    case 'ModActionSetState': return accessor(index, new ModActionSetState())! as ModActionSetState;
    case 'ModActionPurge': return accessor(index, new ModActionPurge())! as ModActionPurge;
    default: return null;
  }
}
